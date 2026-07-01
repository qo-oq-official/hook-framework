#include "collector/class_cache_collector.h"
#include "collector/typed_collector.h"
#include "logging/logger.h"
#include <nlohmann/json.hpp>
#include <format>
#include <Windows.h>
#include <algorithm>
#include <cstdint>

namespace hookfw {

// ── helpers ──────────────────────────────────────────────────────────

static std::string wideToUtf8(const wchar_t* ptr, size_t len) {
    if (len == 0) return "";
    int utf8Len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                      ptr, static_cast<int>(len),
                                      nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return "";
    std::string utf8(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                        ptr, static_cast<int>(len),
                        utf8.data(), utf8Len, nullptr, nullptr);
    return utf8;
}

static bool isReadableMemory(const void* addr, size_t size) {
    if (!addr) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(addr, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return reinterpret_cast<uintptr_t>(addr) + size <=
           reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

static nlohmann::json readClassName(void* crt) {
    nlohmann::json j;
    if (!isReadableMemory(crt, 8)) {
        j["error"] = "unreadable_crt";
        LOG_WARN("readClassName: unreadable CRuntimeClass at {:p}", static_cast<void*>(crt));
        return j;
    }
    auto* namePtr = *reinterpret_cast<uint8_t**>(crt);
    if (!isReadableMemory(namePtr, 1)) {
        j["error"] = "invalid_ptr";
        LOG_WARN("readClassName: invalid namePtr at {:p}", static_cast<void*>(namePtr));
        return j;
    }

    size_t maxLen = 512;
    size_t actualLen = 0;
    for (size_t i = 0; i < maxLen && isReadableMemory(namePtr + i, 1); i++) {
        actualLen = i + 1;
        if (namePtr[i] == 0) break;
    }

    // 1. std::string display (ANSI/ASCII, filter non-printable)
    auto* cptr = reinterpret_cast<const char*>(namePtr);
    size_t cstrLen = strnlen(cptr, actualLen);
    if (cstrLen > 0) {
        std::string s(cptr, cstrLen);
        bool allPrintable = true;
        for (char ch : s) { if (static_cast<unsigned char>(ch) < 0x20 && ch != '\0') { allPrintable = false; break; } }
        if (allPrintable) j["str"] = s;
    }

    // 2. std::wstring display (wide char → UTF-8)
    if (actualLen >= 2) {
        auto* wptr = reinterpret_cast<const wchar_t*>(namePtr);
        size_t wlen = actualLen / 2;
        size_t nullPos = wlen;
        for (size_t i = 0; i < wlen && isReadableMemory(wptr + i, 2); i++) {
            if (wptr[i] == 0) { nullPos = i; break; }
        }
        if (nullPos > 0 && nullPos <= wlen) {
            std::string utf8 = wideToUtf8(wptr, nullPos);
            if (!utf8.empty()) j["wstr"] = utf8;
        }
    }

    // 3. Hex dump (first 32 bytes)
    size_t hexLen = actualLen < 32 ? actualLen : 32;
    std::string hex;
    for (size_t i = 0; i < hexLen; i++) {
        if (!isReadableMemory(namePtr + i, 1)) break;
        char buf[4];
        sprintf_s(buf, "%02X ", static_cast<unsigned int>(namePtr[i]));
        hex += buf;
    }
    if (!hex.empty()) hex.pop_back();
    j["hex"] = hex;

    return j;
}

// su_CPtrArray — swccu.dll, 0x28 bytes (0x20 after vftable)
struct su_CPtrArray {
    void*  __vftable;     // +0x00
    void** m_pData;       // +0x08
    int    m_nSize;       // +0x10
    int    m_nMaxSize;    // +0x14
    int    m_nGrowBy;     // +0x18
};

// CMAP_NODE — swccu.dll, 0x18 bytes (24)
// su_CMapPtrToPtr hash map node (linked-list collision chain)
struct CMAP_NODE {
    CMAP_NODE* pNext;     // +0x00 — next node in chain
    void*      key;       // +0x08 — CRuntimeClass*
    void*      value;     // +0x10 — schema (uint32_t stored as pointer)
};

// su_CMapPtrToPtr — swccu.dll, 0x30 bytes (after vftable at +0x00)
// Schema Remap Table at archive+0x60: CRuntimeClass* → schema
struct su_CMapPtrToPtr {
    void*      __vftable;        // +0x00
    void**     m_pHashTable;     // +0x08 — bucket array (void* per bucket)
    uint64_t   m_nHashTableSize; // +0x10 — number of buckets
    uint64_t   m_nCount;         // +0x18 — total entries
    void*      m_pFreeList;      // +0x20 — free node list
    int        m_nBlockSize;     // +0x28 — nodes per allocation block
};

static nlohmann::json dumpClassList(void* archive) {
    nlohmann::json arr = nlohmann::json::array();
    if (!archive) { arr.push_back({{"error", "null_archive"}});
        LOG_WARN("dumpClassList: null archive"); return arr; }
    if (!isReadableMemory(archive, 0x60)) {
        arr.push_back({{"error", std::format("unreadable_archive@{:#x}", (uint64_t)archive)}});
        LOG_WARN("dumpClassList: unreadable archive at {:p}", archive);
        return arr;
    }
    auto* map = reinterpret_cast<su_CPtrArray*>(
        *reinterpret_cast<void**>((uint8_t*)archive + 0x58));
    if (!map) { arr.push_back({{"error", "null_map_ptr"}});
        LOG_WARN("dumpClassList: null map pointer"); return arr; }
    if (!isReadableMemory(map, sizeof(su_CPtrArray))) {
        arr.push_back({{"error", std::format("unreadable_map@{:#x}", (uint64_t)map)}});
        LOG_WARN("dumpClassList: unreadable map at {:p}", (void*)map);
        return arr;
    }
    bool isStoring = (*(uint8_t*)((uint8_t*)archive + 0x20) & 1) != 0;
    if (!isStoring) {
        arr.push_back({{"note", "loading_mode"}});
        return arr;
    }
    if (map->m_nSize > 0x10000) {
        arr.push_back({{"error", std::format("size_too_large:{}", map->m_nSize)}});
        LOG_WARN("dumpClassList: size too large ({})", map->m_nSize);
        return arr;
    }
    if (!isReadableMemory(map->m_pData, map->m_nSize * sizeof(void*))) {
        arr.push_back({{"error", std::format("unreadable_data@{:#x}", (uint64_t)map->m_pData)}});
        LOG_WARN("dumpClassList: unreadable data at {:p}", (void*)map->m_pData);
        return arr;
    }
    for (int i = 0; i < map->m_nSize; i++) {
        auto* crt = map->m_pData[i];
        arr.push_back({{"idx", i}, {"class", crt ? readClassName(crt) : nlohmann::json{{"error", "null"}}}});
    }
    return arr;
}

static nlohmann::json dumpRemapTable(void* archive) {
    nlohmann::json arr = nlohmann::json::array();
    if (!archive || !isReadableMemory(archive, 0x68)) return arr;

    bool isStoring = (*(uint8_t*)((uint8_t*)archive + 0x20) & 1) != 0;
    if (!isStoring) { arr.push_back({{"note", "loading_mode"}}); return arr; }

    void* remapPtr = *reinterpret_cast<void**>((uint8_t*)archive + 0x60);
    if (!remapPtr) { arr.push_back({{"note", "remap_null_not_created"}}); return arr; }
    if (!isReadableMemory(remapPtr, sizeof(su_CMapPtrToPtr))) {
        arr.push_back({{"error", std::format("unreadable_remap@{:#x}", (uint64_t)remapPtr)}});
        return arr;
    }

    auto* remap = reinterpret_cast<su_CMapPtrToPtr*>(remapPtr);
    if (remap->m_nCount > 0x10000 || remap->m_nHashTableSize > 0x100000) {
        arr.push_back({{"error", std::format("bad_remap_size:{} buckets, {} count",
            remap->m_nHashTableSize, remap->m_nCount)}});
        return arr;
    }
    if (!remap->m_pHashTable || !isReadableMemory(remap->m_pHashTable,
                                                   remap->m_nHashTableSize * sizeof(void*))) {
        arr.push_back({{"error", "unreadable_remap_buckets"}});
        return arr;
    }
    uint32_t entryIdx = 0;
    for (uint64_t b = 0; b < remap->m_nHashTableSize && entryIdx < 500; b++) {
        auto* node = reinterpret_cast<CMAP_NODE*>(remap->m_pHashTable[b]);
        while (node) {
            if (!isReadableMemory(node, sizeof(CMAP_NODE))) break;
            if (node->key) {
                nlohmann::json entry;
                entry["idx"] = entryIdx++;
                entry["class"] = readClassName(node->key);
                entry["schema"] = std::format("0x{:08x}",
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node->value)));
                arr.push_back(entry);
            }
            node = node->pNext;
        }
    }
    return arr;
}

static nlohmann::json counterInfo(void* archive) {
    nlohmann::json cc;
    if (isReadableMemory(archive, 0x68)) {
        cc["counter"] = *reinterpret_cast<uint32_t*>((uint8_t*)archive + 0x50);
        cc["class_list"] = dumpClassList(archive);
        auto remap = dumpRemapTable(archive);
        if (!remap.empty()) cc["schema_remap"] = remap;
    } else {
        cc["error"] = archive
            ? std::format("unreadable@{:#x}", (uint64_t)archive)
            : "null";
        LOG_WARN("counterInfo: unreadable archive at {:p}", archive);
    }
    return cc;
}

// ── ReadClassCollector ───────────────────────────────────────────────

void ReadClassCollector::onExitCustom(const HookContext& ctx, nlohmann::json& args) {
    auto* archive = ctx.arg<void*>(0);
    auto* crtFromRet = ctx.returnValue<void*>();
    auto clsJson = crtFromRet ? readClassName(crtFromRet) : nlohmann::json{{"error", "?"}};
    std::string clsName = clsJson.value("str", clsJson.value("error", "<no_str>"));
    auto cc = counterInfo(archive);
    cc["class"] = clsName;

    auto* pSchema = ctx.arg<void*>(2);
    if (pSchema && isReadableMemory(pSchema, 4))
        cc["schema"] = std::format("0x{:08x}", *reinterpret_cast<uint32_t*>(pSchema));
    auto* pTag = ctx.arg<void*>(3);
    if (pTag && isReadableMemory(pTag, 4))
        cc["tag"] = std::format("0x{:08x}", *reinterpret_cast<uint32_t*>(pTag));

    if (crtFromRet && isReadableMemory(crtFromRet, 8)) {
        uint64_t vt = *reinterpret_cast<uint64_t*>(crtFromRet);
        if (isReadableMemory(reinterpret_cast<void*>(vt), 24)) {
            uint64_t serializeAddr = *reinterpret_cast<uint64_t*>(
                reinterpret_cast<uint8_t*>(vt) + 0x10);
            cc["serializeAddr"] = std::format("{:#x}", serializeAddr);
        } else {
            LOG_WARN("ReadClassCollector: vtable unreadable at {:#x}", vt);
        }
    } else if (crtFromRet) {
        LOG_WARN("ReadClassCollector: return value unreadable at {:p}", crtFromRet);
    }

    args["class_cache"] = cc;
    LOG_TRACE("ReadClass exit archive={:p} class={}", static_cast<void*>(archive), clsName);
}

// ── ReadObjectCollector ──────────────────────────────────────────────

void ReadObjectCollector::onExitCustom(const HookContext& ctx, nlohmann::json& args) {
    auto* obj = ctx.returnValue<void*>();
    if (obj && isReadableMemory(obj, 8)) {
        uint64_t vt = *reinterpret_cast<uint64_t*>(obj);
        if (isReadableMemory(reinterpret_cast<void*>(vt), 24)) {
            uint64_t serializeAddr = *reinterpret_cast<uint64_t*>(
                reinterpret_cast<uint8_t*>(vt) + 0x10);
            args["serializeAddr"] = std::format("{:#x}", serializeAddr);
        } else {
            LOG_WARN("ReadObjectCollector: vtable unreadable at {:#x}", vt);
        }
    } else if (obj) {
        LOG_WARN("ReadObjectCollector: returned object unreadable at {:p}", obj);
    }
}

// ── CopyLoadArrayStoreMapFromCollector ───────────────────────────────

REGISTER_COLLECTOR("copyLoadArrayStoreMapFrom", CopyLoadArrayStoreMapFromCollector);

void CopyLoadArrayStoreMapFromCollector::onEntryCustom(const HookContext& ctx, nlohmann::json& customData) {
    customData["mode"] = "copyLoadArrayStoreMapFrom";
    LOG_TRACE("copyLoadArrayStoreMapFrom entry");
}

void CopyLoadArrayStoreMapFromCollector::onExitCustom(const HookContext& ctx, nlohmann::json& args) {
    auto* src = ctx.arg<void*>(0);
    args["src"] = counterInfo(src);
}

// ── UseCacheArchiveCollector ─────────────────────────────────────────

REGISTER_COLLECTOR("useCacheArchive_c", UseCacheArchiveCollector);

void UseCacheArchiveCollector::onEntryCustom(const HookContext& ctx, nlohmann::json& customData) {
    auto* archive = ctx.arg<void*>(1);
    uint32_t mode = ctx.arg<uint32_t>(2);
    customData["mode"] = mode;
    customData["archive"] = std::format("0x{:016x}", reinterpret_cast<uint64_t>(archive));
    LOG_TRACE("useCacheArchive_c ENTER self={:p} archive={:p} mode={}",
              static_cast<void*>(ctx.arg<void*>(0)), static_cast<void*>(archive), mode);
}

void UseCacheArchiveCollector::onExitCustom(const HookContext& ctx, nlohmann::json& args) {
    auto* archive = ctx.arg<void*>(1);
    args["archive_after"] = counterInfo(archive);
    LOG_TRACE("useCacheArchive_c EXIT self={:p} archive={:p}",
              static_cast<void*>(ctx.arg<void*>(0)), static_cast<void*>(archive));
}

}
