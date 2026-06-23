#include "hook_framework/injector_base.h"
#include "thunk/hook_context.h"
#include "output/trace_output_writer.h"
#include "test_stack_vals.h"
#include <Windows.h>
#include <format>

INJECTOR_TEST_API StackTraceInjectorData g_stackData;

namespace {

class StackTraceInjector : public hookfw::InjectorBase {
public:
    std::string name() const override { return "stack_trace_injector"; }

    void onInject(hookfw::HookContext& ctx, hookfw::TraceOutputWriter* trace) override {
        ctx.captureCallStack(ctx, 16);
        g_stackData.callCount++;

        std::lock_guard lock(g_stackData.mutex);
        g_stackData.lastFrames.clear();
        for (uint32_t i = 0; i < ctx.stackFrameCount; i++) {
            g_stackData.lastFrames.push_back(
                hookfw::HookContext::resolveFrame(ctx.stackFrames[i]));
        }

        if (trace) {
            nlohmann::json jframes = nlohmann::json::array();
            for (const auto& f : g_stackData.lastFrames)
                jframes.push_back(f);
            trace->addTraceEvent("Stack", "hookfw", 'i',
                hookfw::TraceOutputWriter::nowMicroseconds(),
                GetCurrentProcessId(), GetCurrentThreadId(),
                {{"frames", jframes}});
        }
    }
};

REGISTER_INJECTOR("stack_trace_injector", StackTraceInjector);

}
