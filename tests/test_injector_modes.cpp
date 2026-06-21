#include "hook_framework/hook_framework.h"
#include <Windows.h>
#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while(0)

typedef int (*AddFunc)(int, int);
typedef int (*SubFunc)(int, int);

int main() {
    printf("=== Injector Mode Integration Test ===\n");
    printf("Tests: Pre/Post injector modes, injector-only, multi-injector\n\n");

    HMODULE hTarget = LoadLibraryA("test_target.dll");
    CHECK(hTarget != nullptr, "test_target.dll loaded");
    if (!hTarget) return 1;

    auto test_add = reinterpret_cast<AddFunc>(GetProcAddress(hTarget, "test_add"));
    auto test_sub = reinterpret_cast<SubFunc>(GetProcAddress(hTarget, "test_sub"));
    CHECK(test_add != nullptr, "test_add resolved");
    CHECK(test_sub != nullptr, "test_sub resolved");

    auto& engine = hookfw::HookEngine::instance();
    engine.initialize();
    engine.setOutputDirectory("dump_data_injector_modes");

    // ====== Test 1: Default Pre mode ======
    printf("\n--- Test 1: Default Pre mode ---\n");
    {
        bool ok = engine.hook("test_target.dll", "test_add")
            .params({{"a", hookfw::ParamType::Int32}, {"b", hookfw::ParamType::Int32}})
            .returnType(hookfw::ParamType::Int32)
            .injector("test_read_injector")
            .install();
        CHECK(ok, "Default Pre-mode hook installed");
    }

    // ====== Test 2: Explicit Pre mode ======
    printf("\n--- Test 2: Explicit Pre mode ---\n");
    {
        bool ok = engine.hook("test_target.dll", "test_sub")
            .params({{"a", hookfw::ParamType::Int32}, {"b", hookfw::ParamType::Int32}})
            .returnType(hookfw::ParamType::Int32)
            .injector("pre_check_injector")
            .injectorMode(hookfw::InjectorMode::Pre)
            .install();
        CHECK(ok, "Explicit Pre-mode hook installed");
    }

    // ====== Test 3: Post mode ======
    printf("\n--- Test 3: Post mode ---\n");
    {
        bool ok = engine.hook("test_target.dll", "test_sub")
            .params({{"a", hookfw::ParamType::Int32}, {"b", hookfw::ParamType::Int32}})
            .returnType(hookfw::ParamType::Int32)
            .injector("post_check_injector")
            .injectorMode(hookfw::InjectorMode::Post)
            .install();
        CHECK(ok, "Post-mode hook installed");
    }

    // ====== Test 4: Injector-only hook (no collector) ======
    printf("\n--- Test 4: Injector-only hook (no collector/params) ---\n");
    {
        bool ok = engine.hook("test_target.dll", "test_add")
            .returnType(hookfw::ParamType::Int32)
            .injector("test_read_injector")
            .install();
        CHECK(ok, "Injector-only hook installed");
    }

    // ====== Install all ======
    printf("\n--- Install All ---\n");
    CHECK(engine.installAll(), "installAll succeeded");

    // ====== Verify function calls ======
    printf("\n--- Verify function calls ---\n");
    {
        int r1 = test_add(10, 20);
        CHECK(r1 == 30, "test_add(10,20) == 30 (Pre injector + injector-only coexist)");

        int r2 = test_add(7, 8);
        CHECK(r2 == 15, "test_add(7,8) == 15");

        int r3 = test_sub(100, 37);
        CHECK(r3 == 63, "test_sub(100,37) == 63 (Pre + Post injectors coexist)");

        int r4 = test_sub(50, 13);
        CHECK(r4 == 37, "test_sub(50,13) == 37");
    }

    engine.shutdown();

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail;
}
