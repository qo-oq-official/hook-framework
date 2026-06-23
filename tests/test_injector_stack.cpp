#include "hook_framework/hook_framework.h"
#include "test_stack_vals.h"
#include <Windows.h>
#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while(0)

typedef int (*AddFunc)(int, int);

int main() {
    printf("=== Stack Trace Injector Test ===\n\n");

    HMODULE hTarget = LoadLibraryA("test_target.dll");
    CHECK(hTarget != nullptr, "test_target.dll loaded");
    if (!hTarget) return 1;

    auto test_add = reinterpret_cast<AddFunc>(GetProcAddress(hTarget, "test_add"));
    CHECK(test_add != nullptr, "test_add resolved");

    printf("[1] Setting up hook with stack trace injector...\n");
    auto& engine = hookfw::HookEngine::instance();
    engine.initialize();
    engine.setOutputDirectory("dump_data_stack");

    bool ok = engine.hook("test_target.dll", "test_add")
        .injector("stack_trace_injector")
        .injectorMode(hookfw::InjectorMode::Pre)
        .install();
    CHECK(ok, "stack trace injector installed");
    CHECK(engine.installAll(), "installAll succeeded");

    printf("[2] Calling test_add(7,8)...\n");
    int r = test_add(7, 8);
    CHECK(r == 15, "test_add(7,8) == 15");

    printf("[3] Verifying call stack captured...\n");
    int calls = g_stackData.callCount.load();
    CHECK(calls > 0, (std::string("injector called ") + std::to_string(calls) + " time(s)").c_str());

    {   std::lock_guard lock(g_stackData.mutex);
        CHECK(!g_stackData.lastFrames.empty(), "stack frames captured");
        printf("\n  Stack frames (%zu):\n", g_stackData.lastFrames.size());
        bool hasModuleOffset = false;
        for (const auto& f : g_stackData.lastFrames) {
            printf("    %s\n", f.c_str());
            if (f.find(".dll+0x") != std::string::npos || f.find(".exe+0x") != std::string::npos)
                hasModuleOffset = true;
        }
        CHECK(hasModuleOffset, "frames resolved to module+offset format");
        CHECK(g_stackData.lastFrames.size() >= 2, "at least 2 frames");
    }

    engine.shutdown();
    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail;
}
