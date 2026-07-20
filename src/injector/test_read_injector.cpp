#include "hook_framework/injector_base.h"
#include "thunk/hook_context.h"
#include <Windows.h>
#include <format>

namespace {

class TestReadInjector : public hookfw::InjectorBase {
public:
    std::string name() const override { return "test_read_injector"; }

    void onInject(hookfw::HookContext& ctx) override {
        auto cnt = ctx.arg<uint32_t>(1);
        auto bufPtr = ctx.arg<void*>(1);
        (void)cnt;
        (void)bufPtr;
    }
};

REGISTER_INJECTOR("test_read_injector", TestReadInjector);

}
