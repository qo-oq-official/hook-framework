#include "thunk/thunk_manager.h"
#include "thunk/hook_context.h"
#include "engine/hook_engine.h"
#include "output/trace_output_writer.h"
#include "output/hook_data_source.h"
#include "logging/crash_log.h"
#include <Windows.h>
#include <cstring>
#include <cstdio>
#include <format>

static thread_local bool g_inHookDispatch = false;
static thread_local int g_hookDepth = 0;
static volatile uint32_t g_lastHookId = 0;
static volatile uint64_t g_lastPhase = 0;
static volatile uint64_t g_dispatchCount = 0;
static const int MAX_HOOK_DEPTH = 64;

static void dispCrashLog(const char* msg) {
    static bool inited = false;
    if (!inited) {
        inited = true;
        char dllPath[MAX_PATH] = {};
        HMODULE hSelf = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&dispCrashLog), &hSelf);
        GetModuleFileNameA(hSelf, dllPath, MAX_PATH);
        std::string logPath(dllPath);
        auto pos = logPath.rfind('\\');
        if (pos != std::string::npos) logPath = logPath.substr(0, pos + 1);
        logPath += "hook_crash.log";
        hookfw::crash_log::reinit(logPath.c_str(), true);
    }
    hookfw::crash_log::write(msg);
}

static void doDispatchBody(hookfw::ThunkData* data,
                           uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                           uint64_t origRSP, uint64_t phase, void* xmmPtr) {
    auto* hook = hookfw::HookEngineCore::instance().getHookById(data->hookId);
    if (!hook || (!hook->collector && !hook->injector)) return;

    hookfw::HookContext ctx{};
    ctx.hookId = data->hookId;

    if (phase == 0) {
        ctx.rcx = a0;
        ctx.rdx = a1;
        ctx.r8 = a2;
        ctx.r9 = a3;
        ctx.rsp = origRSP;
        if (xmmPtr) {
            memcpy(ctx.xmm0, xmmPtr, 16);
            memcpy(ctx.xmm1, static_cast<uint8_t*>(xmmPtr) + 16, 16);
            memcpy(ctx.xmm2, static_cast<uint8_t*>(xmmPtr) + 32, 16);
            memcpy(ctx.xmm3, static_cast<uint8_t*>(xmmPtr) + 48, 16);
        }
        if (origRSP) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<void*>(origRSP), &mbi, sizeof(mbi))
                && mbi.State == MEM_COMMIT
                && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
                size_t avail = reinterpret_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize
                               - reinterpret_cast<uint8_t*>(origRSP);
                size_t copySize = (avail < 512) ? avail : 512;
                ctx.returnAddress = *reinterpret_cast<uint64_t*>(
                    reinterpret_cast<uint8_t*>(origRSP) - 8);
                memcpy(ctx.stackSnapshot, reinterpret_cast<void*>(origRSP), copySize);
                ctx.stackSnapshotSize = static_cast<uint32_t>(copySize);
            }
        }

        auto writer = std::dynamic_pointer_cast<hookfw::TraceOutputWriter>(
            hookfw::HookEngineCore::instance().getOutputWriter());

        if (hook->injector && hook->descriptor.injectorMode == hookfw::InjectorMode::Pre) {
            hook->injector->onInject(ctx, writer.get());
        }

        if (hook->collector) {
            hook->collector->onEntry(ctx);
        }
    } else {
        ctx.rax = a0;
        ctx.rcx = a1;
        ctx.rdx = a2;
        ctx.r8 = a3;
        ctx.r9 = origRSP;
        if (xmmPtr) {
            memcpy(ctx.xmm0, xmmPtr, 16);
        }

        auto writer = std::dynamic_pointer_cast<hookfw::TraceOutputWriter>(
            hookfw::HookEngineCore::instance().getOutputWriter());

        if (hook->injector && hook->descriptor.injectorMode == hookfw::InjectorMode::Post) {
            hook->injector->onInject(ctx, writer.get());
        }

        if (hook->collector) {
            hook->collector->onExit(ctx);
        }
    }
}

extern "C" {

void hookfw_quick_trace(hookfw::ThunkData* data,
                        uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    if (!data) return;

    std::string name = hookfw::HookDataSource::getHookName(data->hookId);
    nlohmann::json args;
    args["arg0"] = std::format("{:#x}", a0);
    args["arg1"] = std::format("{:#x}", a1);
    args["arg2"] = std::format("{:#x}", a2);
    args["arg3"] = std::format("{:#x}", a3);

    hookfw::HookDataSource::TraceSlice(name, "trace", 'B',
                                        GetCurrentThreadId(), args);
}

void hookfw_quick_trace_exit(hookfw::ThunkData* data,
                             uint64_t retval, uint64_t, uint64_t, uint64_t) {
    if (!data) return;

    std::string name = hookfw::HookDataSource::getHookName(data->hookId);
    nlohmann::json args;
    if (retval != 0) {
        args["return"] = std::format("{:#x}", retval);
    }

    hookfw::HookDataSource::TraceSlice(name, "trace", 'E',
                                        GetCurrentThreadId(), args);
}

void hookfw_dispatch(hookfw::ThunkData* data,
                     uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                     uint64_t origRSP, uint64_t phase, void* xmmPtr) {
    if (g_inHookDispatch) return;
    if (!data) return;

    g_lastHookId = data->hookId;
    g_lastPhase = phase;
    g_dispatchCount++;
    g_inHookDispatch = true;

    __try {
        doDispatchBody(data, a0, a1, a2, a3, origRSP, phase, xmmPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        char buf[256];
        wsprintfA(buf, "[CRASH] hook_id=%u phase=%llu count=%llu code=0x%08X\n",
                  g_lastHookId, g_lastPhase, g_dispatchCount, GetExceptionCode());
        dispCrashLog(buf);
    }

    g_inHookDispatch = false;
}

}
