#include "collector/class_cache_collector.h"
#include "collector/typed_collector.h"
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
    nlohmann::json j;

    for (size_t i = 0; i < params_.size(); i++) {
        auto p = extractParam(ctx, static_cast<int>(i), params_[i]);
        std::string key = p.value("name",
            std::string("arg") + std::to_string(i));
        if (p.contains("value"))
            j[key] = p["value"];
        else if (p.contains("value_hex"))
            j[key] = p["value_hex"];
    }

    auto* archive = ctx.arg<void*>(0);
    j["class_cache"] = buildClassCacheEntry(archive);

    outputWriter_->writeJson(moduleName_, functionName_, 0, "entry", j);
}

void ClassCacheCollector::onExit(const HookContext& ctx) {
    nlohmann::json j;

    auto ret = extractReturnValue(ctx);
    if (ret.contains("value"))
        j["return"] = ret["value"];

    for (size_t i = 0; i < params_.size(); i++) {
        auto p = extractParam(ctx, static_cast<int>(i), params_[i]);
        std::string key = p.value("name",
            std::string("arg") + std::to_string(i));
        if (p.contains("value"))
            j[key] = p["value"];
        else if (p.contains("value_hex"))
            j[key] = p["value_hex"];
    }

    auto* archive = ctx.arg<void*>(0);
    auto* crtFromRet = ctx.returnValue<void*>();
    auto* crtFromParam = ctx.arg<void*>(1);

    std::string clsName = readClassName(crtFromRet);
    if (clsName.empty() && crtFromParam)
        clsName = readClassName(crtFromParam);

    nlohmann::json cc = buildClassCacheEntry(archive);
    cc["class"] = clsName.empty() ? "<null>" : clsName;
    j["class_cache"] = cc;

    outputWriter_->writeJson(moduleName_, functionName_, 0, "exit", j);
}

}
