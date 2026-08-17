#include "thunk/universal_bridge.h"
#include "output/hook_data_source.h"
#include "engine/hook_engine.h"
#include "thunk/hook_context.h"
#include "logging/crash_log.h"
#include "logging/logger.h"
#include <Windows.h>
#include <format>
#include <nlohmann/json.hpp>

namespace hookfw {

uint64_t CallOriginal(ThunkData* data, RegisterContext* ctx, size_t /*stackArgBytes*/) {
    auto* fn = reinterpret_cast<uint64_t(*)(uint64_t,uint64_t,uint64_t,uint64_t)>(
        data->trampolineFunction);
    return fn(ctx->rcx, ctx->rdx, ctx->r8, ctx->r9);
}

void Prehook(InstalledHook *hook, hookfw::ThunkData* data, RegisterContext* ctx)
{
    if (hook && hook->descriptor.mode == HookMode::Trace) {
        std::string name = HookDataSource::getHookName(data->hookId);
        nlohmann::json args;
        args["arg0"] = std::format("{:#x}", ctx->rcx);
        args["arg1"] = std::format("{:#x}", ctx->rdx);
        args["arg2"] = std::format("{:#x}", ctx->r8);
        args["arg3"] = std::format("{:#x}", ctx->r9);
        HookDataSource::TraceSlice(name, "trace", 'B', args);
    } else if (hook && hook->collector) {
        HookContext hctx{};
        hctx.hookId = data->hookId;
        hctx.rcx = ctx->rcx;
        hctx.rdx = ctx->rdx;
        hctx.r8 = ctx->r8;
        hctx.r9 = ctx->r9;
        hctx.rsp = ctx->rsp;
        try {
            hook->collector->onEntry(hctx);
        } catch (...) {
            LOG_ERROR("UniversalCxxBridge: onEntry crashed");
        }
    }
}

void Posthook(InstalledHook *hook, hookfw::ThunkData* data, RegisterContext* ctx, bool threw, uint64_t retval)
{
    if (hook && hook->descriptor.mode == HookMode::Trace) {
        std::string name = HookDataSource::getHookName(data->hookId);
        nlohmann::json args;
        if (!threw && retval != 0)
            args["return"] = std::format("{:#x}", retval);
        if (threw)
            args["exception"] = true;
        HookDataSource::TraceSlice(name, "trace", 'E', args);
    } else if (hook && hook->collector) {
        HookContext hctx{};
        hctx.hookId = data->hookId;
        hctx.rax = threw ? 0 : retval;
        hctx.rcx = ctx->rcx;
        hctx.rdx = ctx->rdx;
        hctx.r8 = ctx->r8;
        hctx.r9 = ctx->r9;
        try {
            hook->collector->onExit(hctx);
        } catch (...) {
            LOG_ERROR("UniversalCxxBridge: onExit crashed");
        }
    }
}

}  // namespace hookfw

extern "C" void UniversalCxxBridge(hookfw::ThunkData* data, RegisterContext* ctx) {
    using namespace hookfw;

    auto* hook = HookEngineCore::instance().getHookById(data->hookId);
    uint64_t retval = 0;
    bool threw = false;
    std::string name = HookDataSource::getHookName(data->hookId);
    // --- Pre-hook ---
    Prehook(hook, data, ctx);
    // --- Call original function with exception handling ---
    try {
        retval = CallOriginal(data, ctx, 0);
    } catch (...) {
        Posthook(hook, data, ctx, true, 0);
        LOG_DEBUG("CallOriginal {} threw", name);
        throw;
    }
    Posthook(hook, data, ctx, false, retval);
}
