#include "hook_framework/injector_base.h"
#include "thunk/hook_context.h"
#include "test_struct_target.h"
#include "test_injector_vals.h"
#include <atomic>
#include <string>

INJECTOR_TEST_API InjectorValues g_values;

namespace {

class PreStructInjector : public hookfw::InjectorBase {
public:
    std::string name() const override { return "pre_struct_injector"; }

    void onInject(hookfw::HookContext& ctx, hookfw::TraceOutputWriter*) override {
        auto* obj = ctx.arg<TestObjectA*>(0);
        if (!obj) return;

        g_values.pre_x     = obj->x;
        g_values.pre_flag  = static_cast<int>(obj->flag);
        g_values.pre_y_int = static_cast<int>(obj->y * 100);
        g_values.pre_name  = obj->name;
        g_values.pre_wname = obj->wname;
    }
};

class PostStructInjector : public hookfw::InjectorBase {
public:
    std::string name() const override { return "post_struct_injector"; }

    void onInject(hookfw::HookContext& ctx, hookfw::TraceOutputWriter*) override {
        auto* obj = ctx.arg<TestObjectA*>(0);
        if (!obj) return;

        g_values.post_x     = obj->x;
        g_values.post_flag  = static_cast<int>(obj->flag);
        g_values.post_y_int = static_cast<int>(obj->y * 100);
        g_values.post_name  = obj->name;
        g_values.post_wname = obj->wname;
    }
};

REGISTER_INJECTOR("pre_struct_injector", PreStructInjector);
REGISTER_INJECTOR("post_struct_injector", PostStructInjector);

}
