#include "collector/class_cache_collector.h"
#include "collector/collector_base.h"
#include "logging/logger.h"
#include "output/file_output_writer.h"
#include "output/trace_output_writer.h"
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

void ClassCacheCollector::onEntry(const HookContext& ctx) {
    auto* archive = ctx.arg<void*>(0);
    if (!isReadableMemory(archive, 0x60)) return;

    uint32_t counter = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(archive) + 0x50);

    nlohmann::json j;
    j["archive"] = std::format("{:#x}", reinterpret_cast<uintptr_t>(archive));
    j["phase"] = "entry";
    j["counter"] = counter;

    outputWriter_->writeJson(moduleName_, functionName_, 0, "entry", j);
}

void ClassCacheCollector::onExit(const HookContext& ctx) {
    auto* archive = ctx.arg<void*>(0);
    if (!isReadableMemory(archive, 0x60)) return;

    uint32_t counter = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(archive) + 0x50);

    auto* crtFromParam = ctx.arg<void*>(1);
    auto* crtFromRet = ctx.returnValue<void*>();

    std::string clsName = readClassName(crtFromRet);
    if (clsName.empty() && crtFromParam)
        clsName = readClassName(crtFromParam);

    nlohmann::json j;
    j["archive"] = std::format("{:#x}", reinterpret_cast<uintptr_t>(archive));
    j["phase"] = "exit";
    j["counter_post"] = counter;
    j["class"] = clsName.empty() ? "<null>" : clsName;

    outputWriter_->writeJson(moduleName_, functionName_, 0, "exit", j);
}

}
