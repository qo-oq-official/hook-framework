#include "thunk/universal_bridge.h"
#include "output/ring_buffer.h"
#include "output/trace_flusher.h"
#include "engine/hook_engine.h"
#include "thunk/hook_context.h"
#include "logging/crash_log.h"
#include <Windows.h>
#include <cstring>

namespace hookfw {

uint64_t CallOriginal(ThunkData* data, RegisterContext* ctx, size_t /*stackArgBytes*/) {
    auto* fn = reinterpret_cast<uint64_t(*)(uint64_t,uint64_t,uint64_t,uint64_t)>(
        data->trampolineFunction);
    return fn(ctx->rcx, ctx->rdx, ctx->r8, ctx->r9);
}

void __fastcall UniversalCxxBridge(ThunkData* data, RegisterContext* ctx) {
    auto* hook = HookEngineCore::instance().getHookById(data->hookId);
    uint64_t retval = 0;
    bool threw = false;

    // --- Pre-hook ---
    if (hook && hook->descriptor.mode == HookMode::Trace) {
        TraceRecord rec{};
        rec.hookId = data->hookId;
        rec.threadId = GetCurrentThreadId();
        rec.timestampUs = RingBuffer::nowMicroseconds();
        rec.arg0 = ctx->rcx;
        rec.arg1 = ctx->rdx;
        rec.arg2 = ctx->r8;
        rec.arg3 = ctx->r9;
        rec.phase = 0;
        TraceFlusher::instance().ringBuffer().push(rec);
    } else if (hook && hook->collector) {
        HookContext hctx{};
        hctx.hookId = data->hookId;
        hctx.rcx = ctx->rcx;
        hctx.rdx = ctx->rdx;
        hctx.r8 = ctx->r8;
        hctx.r9 = ctx->r9;
        hctx.rsp = ctx->rsp;
        if (hook->injector && hook->descriptor.injectorMode == InjectorMode::Pre) {
            hook->injector->onInject(hctx);
        }
        try {
            hook->collector->onEntry(hctx);
        } catch (...) {
            crash_log::write("UniversalCxxBridge: onEntry crashed\n");
        }
    }

    // --- Call original function with full exception handling ---
    try {
        retval = CallOriginal(data, ctx, 0);
    } catch (...) {
        threw = true;
        retval = 0;
    }

    ctx->rax = threw ? 0 : retval;

    // --- Post-hook (executed for BOTH normal and exception paths) ---
    if (hook && hook->descriptor.mode == HookMode::Trace) {
        TraceRecord rec{};
        rec.hookId = data->hookId;
        rec.threadId = GetCurrentThreadId();
        rec.timestampUs = RingBuffer::nowMicroseconds();
        rec.arg0 = threw ? 0 : retval;
        rec.arg1 = 0;
        rec.arg2 = 0;
        rec.arg3 = 0;
        rec.phase = 1;
        TraceFlusher::instance().ringBuffer().push(rec);
    } else if (hook && hook->collector) {
        HookContext hctx{};
        hctx.hookId = data->hookId;
        hctx.rax = threw ? 0 : retval;
        hctx.rcx = ctx->rcx;
        hctx.rdx = ctx->rdx;
        hctx.r8 = ctx->r8;
        hctx.r9 = ctx->r9;
        if (hook->injector && hook->descriptor.injectorMode == InjectorMode::Post) {
            hook->injector->onInject(hctx);
        }
        try {
            hook->collector->onExit(hctx);
        } catch (...) {
            crash_log::write("UniversalCxxBridge: onExit crashed\n");
        }
    }

    // Per-thread flush
    static thread_local uint32_t t_counter = 0;
    if (++t_counter >= 50000) {
        t_counter = 0;
        TraceFlusher::instance().syncFlush();
    }

    // Re-throw original exception
    if (threw)
        throw;
}

}
