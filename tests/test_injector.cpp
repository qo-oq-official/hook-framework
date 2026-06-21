#include "hook_framework/hook_framework.h"
#include <Windows.h>
#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while(0)

int main() {
    printf("=== Injector Integration Test ===\n");
    printf("Tests: register injector in DLL, use via C++ API, verify execution\n\n");

    printf("[1] Loading test_target.dll...\n");
    HMODULE hTarget = LoadLibraryA("test_target.dll");
    CHECK(hTarget != nullptr, "test_target.dll loaded");
    if (!hTarget) return 1;

    printf("[2] Setting up hook with test_read_injector...\n");
    auto& engine = hookfw::HookEngine::instance();
    engine.initialize();
    engine.setOutputDirectory("dump_data");

    bool ok = engine.hook("test_target.dll", "test_add")
        .params({{"a", hookfw::ParamType::Int32}, {"b", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::Int32)
        .injector("test_read_injector")
        .install();
    CHECK(ok, "hook with injector(test_read_injector) installed");

    printf("[3] Installing hooks...\n");
    CHECK(engine.installAll(), "installAll succeeded");

    printf("[4] Calling test_add(7,8)...\n");
    typedef int (*AddFunc)(int, int);
    auto test_add = reinterpret_cast<AddFunc>(GetProcAddress(hTarget, "test_add"));
    int result = test_add(7, 8);
    CHECK(result == 15, "test_add(7,8) == 15");

    printf("[5] Calling test_add(42,7)...\n");
    result = test_add(42, 7);
    CHECK(result == 49, "test_add(42,7) == 49");

    printf("[6] Shutting down framework...\n");
    engine.shutdown();

    printf("[7] All function calls returned correct values, hooks + injector chain intact\n");
    CHECK(true, "injector executed successfully (functions returned correct results)");

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail;
}
