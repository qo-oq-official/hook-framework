#include "hook_framework/hook_framework.h"
#include "test_stack_vals.h"
#include <Windows.h>
#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while(0)

typedef int (*DeepFunc)(int);

int main() {
    printf("=== Stack Trace Injector Test (Deep Call Chain) ===\n\n");

    HMODULE hTarget = LoadLibraryA("test_target.dll");
    CHECK(hTarget != nullptr, "test_target.dll loaded");
    if (!hTarget) return 1;

    auto test_deep = reinterpret_cast<DeepFunc>(GetProcAddress(hTarget, "test_deep_call"));
    auto deep_l3   = reinterpret_cast<DeepFunc>(GetProcAddress(hTarget, "deep_call_l3"));
    CHECK(test_deep != nullptr, "test_deep_call resolved");
    CHECK(deep_l3 != nullptr, "deep_call_l3 resolved");

    printf("[1] Setting up hook on deep_call_l3 (innermost) with stack injector...\n");
    auto& engine = hookfw::HookEngine::instance();
    engine.initialize();
    engine.setOutputDirectory("dump_data_stack");

    bool ok = engine.hook("test_target.dll", "deep_call_l3")
        .injector("stack_trace_injector")
        .injectorMode(hookfw::InjectorMode::Pre)
        .install();
    CHECK(ok, "stack trace injector installed on deep_call_l3");
    CHECK(engine.installAll(), "installAll succeeded");

    printf("[2] Calling test_deep_call(5) → deep_call_l1 → l2 → l3 (hooked)...\n");
    int r = test_deep(5);
    CHECK(r == 13, "test_deep_call(5) == 13 (5*2+1+1+1)");

    printf("[3] Verifying deep call stack captured...\n");
    int calls = g_stackData.callCount.load();
    CHECK(calls > 0, (std::string("injector called ") + std::to_string(calls) + " time(s)").c_str());

    {   std::lock_guard lock(g_stackData.mutex);
        CHECK(!g_stackData.lastFrames.empty(), "stack frames captured");
        printf("\n  Stack frames (%zu):\n", g_stackData.lastFrames.size());
        for (const auto& f : g_stackData.lastFrames)
            printf("    %s\n", f.c_str());

        CHECK(g_stackData.lastFrames.size() >= 4,
              (std::string("at least 4 frames (got ") + std::to_string(g_stackData.lastFrames.size()) + ")").c_str());
        CHECK(g_stackData.lastFrames[0].find(".exe+") != std::string::npos ||
              g_stackData.lastFrames[0].find(".dll+") != std::string::npos,
              "frame 0 resolved to module+offset");
    }

    engine.shutdown();
    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail;
}
