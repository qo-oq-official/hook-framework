#include "collector/class_cache_collector.h"
#include "collector/typed_collector.h"
#include "collector/param_serializer.h"
#include "logging/logger.h"
#include <nlohmann/json.hpp>
#include <format>
#include <Windows.h>

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

static std::string readClassName(void* crt) {
    if (!isReadableMemory(crt, 8)) return "";
    auto* namePtr = *reinterpret_cast<const char**>(crt);
    if (!isReadableMemory(namePtr, 1)) return "";
    return namePtr;
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
    if (!archive) { arr.push_back({{"error", "null_archive"}}); return arr; }
    if (!isReadableMemory(archive, 0x60)) {
        arr.push_back({{"error", std::format("unreadable_archive@{:#x}", (uint64_t)archive)}});
        return arr;
    }
    auto* map = reinterpret_cast<su_CPtrArray*>(
        *reinterpret_cast<void**>((uint8_t*)archive + 0x58));
    if (!map) { arr.push_back({{"error", "null_map_ptr"}}); return arr; }
    if (!isReadableMemory(map, sizeof(su_CPtrArray))) {
        arr.push_back({{"error", std::format("unreadable_map@{:#x}", (uint64_t)map)}});
        return arr;
    }
    bool isStoring = (*(uint8_t*)((uint8_t*)archive + 0x20) & 1) != 0;
    if (!isStoring) {
        arr.push_back({{"note", "loading_mode"}});
        return arr;
    }
    if (map->m_nSize > 0x10000) {
        arr.push_back({{"error", std::format("size_too_large:{}", map->m_nSize)}});
        return arr;
    }
    if (!isReadableMemory(map->m_pData, map->m_nSize * sizeof(void*))) {
        arr.push_back({{"error", std::format("unreadable_data@{:#x}", (uint64_t)map->m_pData)}});
        return arr;
    }
    for (int i = 0; i < map->m_nSize; i++) {
        auto* crt = map->m_pData[i];
        arr.push_back({{"idx", i}, {"class", crt ? readClassName(crt) : "(null)"}});
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
    std::string clsName = crtFromRet ? readClassName(crtFromRet) : "<?>";
    auto cc = counterInfo(archive);
    cc["class"] = clsName;
    args["class_cache_after"] = cc;
    outputWriter_->writeJson(moduleName_, functionName_, 0, "exit", args);
    LOG_TRACE("ReadClass exit archive={:p} class={}", static_cast<void*>(archive), clsName);
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
