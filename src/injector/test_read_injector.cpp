#include "hook_framework/injector_base.h"
#include "thunk/hook_context.h"
#include "output/trace_output_writer.h"
#include <Windows.h>
#include <format>

namespace {

class TestReadInjector : public hookfw::InjectorBase {
public:
    std::string name() const override { return "test_read_injector"; }

    void onInject(hookfw::HookContext& ctx, hookfw::TraceOutputWriter* trace) override {
        // Read the count parameter (index 1 = RDX for x64 member functions)
        auto cnt = ctx.arg<uint32_t>(1);

        // Read buffer pointer (index 0 = RCX = this, index 1 = RDX = buffer ptr)
        // Actually, for su_CArchive::Read: RCX=this, RDX=buffer, R8=count
        auto bufPtr = ctx.arg<void*>(1);

        if (trace) {
            nlohmann::json args;
            args["count"] = cnt;
            args["buf_ptr"] = std::format("{:#x}", reinterpret_cast<uintptr_t>(bufPtr));
            args["injected"] = true;

            trace->addTraceEvent(
                "Injector: test_read_injector",
                "hookfw",
                'i',
                hookfw::TraceOutputWriter::nowMicroseconds(),
                GetCurrentProcessId(),
                GetCurrentThreadId(),
                args
            );
        }
    }
};

REGISTER_INJECTOR("test_read_injector", TestReadInjector);

}
