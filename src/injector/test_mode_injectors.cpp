#include "hook_framework/injector_base.h"
#include "thunk/hook_context.h"
#include "output/trace_output_writer.h"
#include <Windows.h>
#include <format>
#include <atomic>

namespace {

// Pre mode: records that it ran BEFORE the original function
class PreCheckInjector : public hookfw::InjectorBase {
public:
    std::string name() const override { return "pre_check_injector"; }

    void onInject(hookfw::HookContext& ctx, hookfw::TraceOutputWriter* trace) override {
        // Read param before original execution
        auto a = ctx.arg<int>(0);
        auto b = ctx.arg<int>(1);

        // Store a key into return value slot as a marker
        // (the original function will overwrite this)
        ctx.setReturnValue(-999);
        ctx.setArg(0, a + 1000);  // modify argument for test validation

        if (trace) {
            nlohmann::json args;
            args["mode"] = "pre";
            args["param_a"] = a;
            args["param_b"] = b;
            trace->addTraceEvent(
                "Injector: pre_check",
                "hookfw", 'i',
                hookfw::TraceOutputWriter::nowMicroseconds(),
                GetCurrentProcessId(), GetCurrentThreadId(), args);
        }
    }
};

// Post mode: records that it ran AFTER the original function
class PostCheckInjector : public hookfw::InjectorBase {
public:
    std::string name() const override { return "post_check_injector"; }

    void onInject(hookfw::HookContext& ctx, hookfw::TraceOutputWriter* trace) override {
        // At exit: ctx.rax holds the original return value
        auto retval = ctx.returnValue<int>();

        // Read the original args (still in rcx/rdx at exit phase)
        auto a = ctx.arg<int>(0);
        auto b = ctx.arg<int>(1);

        // Verify original function returned correctly
        if (retval == a + b) {
            // Override return value to prove injector-visible modification
            ctx.setReturnValue(retval * 10);
        }

        if (trace) {
            nlohmann::json args;
            args["mode"] = "post";
            args["original_return"] = retval;
            args["modified_to"] = retval * 10;
            trace->addTraceEvent(
                "Injector: post_check",
                "hookfw", 'i',
                hookfw::TraceOutputWriter::nowMicroseconds(),
                GetCurrentProcessId(), GetCurrentThreadId(), args);
        }
    }
};

// Pre-order counter: tracks call order
class OrderTracker : public hookfw::InjectorBase {
public:
    std::string name() const override { return "order_tracker_pre"; }

    static std::atomic<int> s_preCount;
    static std::atomic<int> s_postCount;

    void onInject(hookfw::HookContext&, hookfw::TraceOutputWriter*) override {
        s_preCount++;
    }

    static void reset() { s_preCount = 0; s_postCount = 0; }
};

std::atomic<int> OrderTracker::s_preCount{0};
std::atomic<int> OrderTracker::s_postCount{0};

// Post-order counter
class OrderTrackerPost : public hookfw::InjectorBase {
public:
    std::string name() const override { return "order_tracker_post"; }

    void onInject(hookfw::HookContext&, hookfw::TraceOutputWriter*) override {
        OrderTracker::s_postCount++;
    }
};

REGISTER_INJECTOR("pre_check_injector", PreCheckInjector);
REGISTER_INJECTOR("post_check_injector", PostCheckInjector);
REGISTER_INJECTOR("order_tracker_pre", OrderTracker);
REGISTER_INJECTOR("order_tracker_post", OrderTrackerPost);

}
