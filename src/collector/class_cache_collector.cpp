#include "collector/class_cache_collector.h"
#include "collector/typed_collector.h"
#include "collector/param_serializer.h"
#include "logging/logger.h"
#include <nlohmann/json.hpp>
#include <format>
#include <Windows.h>

namespace hookfw {

REGISTER_COLLECTOR("class_cache", ClassCacheCollector);

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

static nlohmann::json buildClassCacheEntry(void* archive) {
    nlohmann::json cc;
    if (isReadableMemory(archive, 0x60)) {
        uint32_t counter = *reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(archive) + 0x50);
        cc["counter"] = counter;
    }
    return cc;
}

void ClassCacheCollector::onEntry(const HookContext& ctx) {
    nlohmann::json args;

    if (!params_.empty()) {
        ParamSerializer::addConfigEntry(args, ctx, params_);
    } else {
        ParamSerializer::addDefaultEntry(args, ctx);
    }

    auto* archive = ctx.arg<void*>(0);
    args["class_cache"] = buildClassCacheEntry(archive);

    outputWriter_->writeJson(moduleName_, functionName_, 0, "entry", args);
}

void ClassCacheCollector::onExit(const HookContext& ctx) {
    nlohmann::json args;

    if (!params_.empty()) {
        ParamSerializer::addConfigExit(args, ctx, params_, returnType_);
    } else {
        ParamSerializer::addDefaultExit(args, ctx);
    }

    auto* archive = ctx.arg<void*>(0);
    auto* crtFromRet = ctx.returnValue<void*>();
    auto* crtFromParam = ctx.arg<void*>(1);

    std::string clsName = readClassName(crtFromRet);
    if (clsName.empty() && crtFromParam)
        clsName = readClassName(crtFromParam);

    nlohmann::json cc = buildClassCacheEntry(archive);
    cc["class"] = clsName.empty() ? "<null>" : clsName;
    args["class_cache"] = cc;

    outputWriter_->writeJson(moduleName_, functionName_, 0, "exit", args);
}

}
