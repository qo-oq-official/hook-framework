#include "hook_framework/hook_framework.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while(0)

typedef const char* (*Func6)(int, int, int, int, int, int*);

// Replacement: adds instead of multiplies
static const char* replace_add5_out(int a, int b, int c, int d, int e, int* out) {
    if (out) *out = a + b + c + d + e;
    static char buf[32];
    sprintf_s(buf, "%d", out ? *out : 0);
    return buf;
}

int main() {
    printf("=== Replacement Stack Forwarding Test ===\n");
    printf("Tests: 6-arg function with replace (stack params e + out)\n\n");

    HMODULE hTarget = LoadLibraryA("test_target.dll");
    CHECK(hTarget != nullptr, "test_target.dll loaded");
    if (!hTarget) return 1;

    auto test_mul5 = reinterpret_cast<Func6>(
        GetProcAddress(hTarget, "test_mul5_out"));
    CHECK(test_mul5 != nullptr, "test_mul5_out resolved");

    // ===== Before hook: verify original behavior =====
    printf("\n[1] Before hook: test_mul5_out(2,3,4,5,6,&out)...\n");
    {
        int out = 0;
        const char* ret = test_mul5(2, 3, 4, 5, 6, &out);
        CHECK(out == 720, "original: out = 2*3*4*5*6 = 720");
        CHECK(strcmp(ret, "720") == 0, "original: return '720'");
    }

    // ===== Install replacement =====
    printf("\n[2] Installing replacement (add5 instead of mul5)...\n");
    auto& engine = hookfw::HookEngine::instance();
    engine.initialize();
    engine.setOutputDirectory("dump_data_replace");

    bool ok = engine.hook("test_target.dll", "test_mul5_out")
        .replace(reinterpret_cast<void*>(replace_add5_out))
        .install();
    CHECK(ok, "replace hook installed");

    CHECK(engine.installAll(), "installAll succeeded");

    // ===== After hook: verify replacement behavior =====
    printf("\n[3] After hook: test_mul5_out(2,3,4,5,6,&out)...\n");
    {
        int out = 0;
        const char* ret = test_mul5(2, 3, 4, 5, 6, &out);
        // replace_add5_out does addition: 2+3+4+5+6 = 20
        CHECK(out == 20, "replaced: out = 2+3+4+5+6 = 20");
        CHECK(strcmp(ret, "20") == 0, "replaced: return '20'");
    }

    // ===== Verify stack param e=6 was received =====
    printf("\n[4] Verifying stack param e was forwarded correctly...\n");
    {
        // Test with different values to ensure all params reach replacement
        int out = 0;
        const char* ret = test_mul5(10, 20, 30, 40, 50, &out);
        CHECK(out == 150, "replaced: out = 10+20+30+40+50 = 150");
        CHECK(strcmp(ret, "150") == 0, "replaced: return '150'");
    }

    // ===== Verify output param (6th arg on stack) works =====
    printf("\n[5] Verifying 6th stack param (output pointer) works...\n");
    {
        int out = -1;
        test_mul5(0, 0, 0, 0, 0, &out);
        CHECK(out == 0, "output param was written by replacement");
    }

    engine.shutdown();

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail;
}
