#include "hook_framework/injector_base.h"
#include "thunk/hook_context.h"
#include <Windows.h>
#include <format>
#include <atomic>

namespace {

// Pre mode: records that it ran BEFORE the original function
class PreCheckInjector : public hookfw::InjectorBase {
public:
    std::string name() const override { return "pre_check_injector"; }

    void onInject(hookfw::HookContext& ctx) override {
        auto a = ctx.arg<int>(0);
        auto b = ctx.arg<int>(1);

        ctx.setReturnValue(-999);
        ctx.setArg(0, a + 1000);
    }
};

// Post mode: records that it ran AFTER the original function
class PostCheckInjector : public hookfw::InjectorBase {
public:
    std::string name() const override { return "post_check_injector"; }

    void onInject(hookfw::HookContext& ctx) override {
        auto retval = ctx.returnValue<int>();
        auto a = ctx.arg<int>(0);
        auto b = ctx.arg<int>(1);

        if (retval == a + b) {
            ctx.setReturnValue(retval * 10);
        }
    }
};

// Pre-order counter: tracks call order
class OrderTracker : public hookfw::InjectorBase {
public:
    std::string name() const override { return "order_tracker_pre"; }

    static std::atomic<int> s_preCount;
    static std::atomic<int> s_postCount;

    void onInject(hookfw::HookContext&) override {
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

    void onInject(hookfw::HookContext&) override {
        OrderTracker::s_postCount++;
    }
};

REGISTER_INJECTOR("pre_check_injector", PreCheckInjector);
REGISTER_INJECTOR("post_check_injector", PostCheckInjector);
REGISTER_INJECTOR("order_tracker_pre", OrderTracker);
REGISTER_INJECTOR("order_tracker_post", OrderTrackerPost);

}
