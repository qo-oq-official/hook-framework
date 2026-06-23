#include "collector/class_cache_collector.h"
#include "collector/typed_collector.h"
#include "collector/param_serializer.h"
#include "logging/logger.h"
#include <nlohmann/json.hpp>
#include <format>
#include <Windows.h>
#include <algorithm>

namespace hookfw {

// ── helpers ──────────────────────────────────────────────────────────

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

// su_CPtrArray — swccu.dll, 0x20 bytes
struct su_CPtrArray {
    void*  __vftable;     // +0x00
    void** m_pData;       // +0x08
    int    m_nSize;       // +0x10
    int    m_nMaxSize;    // +0x14
    int    m_nGrowBy;     // +0x18
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

static nlohmann::json counterInfo(void* archive) {
    nlohmann::json cc;
    if (isReadableMemory(archive, 0x60)) {
        cc["counter"] = *reinterpret_cast<uint32_t*>((uint8_t*)archive + 0x50);
        cc["class_list"] = dumpClassList(archive);
    } else {
        cc["error"] = archive
            ? std::format("unreadable@{:#x}", (uint64_t)archive)
            : "null";
        LOG_WARN("counterInfo: unreadable archive at {:p}", archive);
    }
    return cc;
}

// ── ReadClassCollector ───────────────────────────────────────────────

REGISTER_COLLECTOR("ReadClass", ReadClassCollector);

void ReadClassCollector::onEntry(const HookContext& ctx) {
    nlohmann::json args;
    ParamSerializer::addDefaultEntry(args, ctx);
    ParamSerializer::addConfigEntry(args, ctx, params_);

    auto* archive = ctx.arg<void*>(0);
    outputWriter_->writeJson(moduleName_, functionName_, 0, "entry", args);
    LOG_TRACE("ReadClass entry archive={:p}", static_cast<void*>(archive));
}

void ReadClassCollector::onExit(const HookContext& ctx) {
    nlohmann::json args;
    ParamSerializer::addDefaultExit(args, ctx);
    ParamSerializer::addConfigExit(args, ctx, params_, returnType_);
    auto* archive = ctx.arg<void*>(0);
    auto* crtFromRet = ctx.returnValue<void*>();
    auto clsJson = crtFromRet ? readClassName(crtFromRet) : nlohmann::json{{"error", "?"}};
    std::string clsName = clsJson.value("str", clsJson.value("error", "<no_str>"));
    auto cc = counterInfo(archive);
    cc["class"] = clsName;

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

    args["class_cache_after"] = cc;

    if (clsName == "moHeader_c" || clsName == "moLogs_c") {
        nlohmann::json jframes = nlohmann::json::array();
        if (ctx.returnAddress)
            jframes.push_back(hookfw::HookContext::resolveFrame(ctx.returnAddress));
        void* bt[15] = {};
        uint16_t n = RtlCaptureStackBackTrace(0, 15, bt, NULL);
        for (uint16_t i = 0; i < n; i++)
            jframes.push_back(hookfw::HookContext::resolveFrame(reinterpret_cast<uint64_t>(bt[i])));
        args["call_stack"] = jframes;
    }

    outputWriter_->writeJson(moduleName_, functionName_, 0, "exit", args);
    LOG_TRACE("ReadClass exit archive={:p} class={}", static_cast<void*>(archive), clsName);
}

// ── ReadObjectCollector ──────────────────────────────────────────────

REGISTER_COLLECTOR("ReadObject", ReadObjectCollector);

void ReadObjectCollector::onEntry(const HookContext& ctx) {
    nlohmann::json args;
    ParamSerializer::addDefaultEntry(args, ctx);
    ParamSerializer::addConfigEntry(args, ctx, params_);
    outputWriter_->writeJson(moduleName_, functionName_, 0, "entry", args);
}

void ReadObjectCollector::onExit(const HookContext& ctx) {
    nlohmann::json args;
    ParamSerializer::addDefaultExit(args, ctx);
    ParamSerializer::addConfigExit(args, ctx, params_, returnType_);

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

    outputWriter_->writeJson(moduleName_, functionName_, 0, "exit", args);
}

// ── CopyLoadArrayStoreMapFromCollector ───────────────────────────────

REGISTER_COLLECTOR("copyLoadArrayStoreMapFrom", CopyLoadArrayStoreMapFromCollector);

void CopyLoadArrayStoreMapFromCollector::onEntry(const HookContext& ctx) {
    nlohmann::json args;
    ParamSerializer::addConfigEntry(args, ctx, params_);
    outputWriter_->writeJson(moduleName_, functionName_, 0, "entry", args);
    LOG_TRACE("copyLoadArrayStoreMapFrom entry");
}

void CopyLoadArrayStoreMapFromCollector::onExit(const HookContext& ctx) {
    auto* src = ctx.arg<void*>(0);
    nlohmann::json args;
    args["phase"] = "exit";
    args["src"] = counterInfo(src);
    outputWriter_->writeJson(moduleName_, functionName_, 0, "exit", args);
}

// ── UseCacheArchiveCollector ─────────────────────────────────────────

REGISTER_COLLECTOR("useCacheArchive_c", UseCacheArchiveCollector);

void UseCacheArchiveCollector::onEntry(const HookContext& ctx) {
    auto* self = ctx.arg<void*>(0);   // this
    auto* archive = ctx.arg<void*>(1);
    uint32_t mode = ctx.arg<uint32_t>(2);
    nlohmann::json args;

    ParamSerializer::addConfigEntry(args, ctx, params_);
    args["phase"] = "entry";
    args["mode"] = mode;
    outputWriter_->writeJson(moduleName_, functionName_, 0, "entry", args);
    LOG_TRACE("useCacheArchive_c ENTER self={:p} archive={:p} mode={}",
              static_cast<void*>(self), static_cast<void*>(archive), mode);
}

void UseCacheArchiveCollector::onExit(const HookContext& ctx) {
    auto* self = ctx.arg<void*>(0);
    auto* archive = ctx.arg<void*>(1);
    nlohmann::json args;
    args["phase"] = "exit";
    args["archive_after"] = counterInfo(archive);
    outputWriter_->writeJson(moduleName_, functionName_, 0, "exit", args);
    LOG_TRACE("useCacheArchive_c EXIT self={:p} archive={:p}",
              static_cast<void*>(self), static_cast<void*>(archive));
}

}
