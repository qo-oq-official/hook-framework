#include <hook_framework/hook_framework.h>
#include <Windows.h>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

static const char* DUMP_DIR = "test_dump_replace";

typedef int (*FnAdd)(int, int);
typedef int (*FnSub)(int, int);

static int g_testsPassed = 0;
static int g_testsFailed = 0;

void check(bool condition, const char* testName) {
    if (condition) {
        std::cout << "  [PASS] " << testName << "\n";
        g_testsPassed++;
    } else {
        std::cout << "  [FAIL] " << testName << "\n";
        g_testsFailed++;
    }
}

static int hooked_add(int a, int b) {
    return (a + b) * 10;
}

static int hooked_sub(int a, int b) {
    return 999;
}

int main() {
    std::cout << "=== Hook Framework Replace Test ===\n\n";

    if (fs::exists(DUMP_DIR)) {
        fs::remove_all(DUMP_DIR);
    }

    HMODULE hTarget = LoadLibraryA("test_target.dll");
    if (!hTarget) {
        std::cerr << "FATAL: Cannot load test_target.dll, error=" << GetLastError() << "\n";
        return 1;
    }

    auto fnAdd = reinterpret_cast<FnAdd>(GetProcAddress(hTarget, "test_add"));
    auto fnSub = reinterpret_cast<FnSub>(GetProcAddress(hTarget, "test_sub"));

    if (!fnAdd || !fnSub) {
        std::cerr << "FATAL: Cannot resolve test functions\n";
        return 1;
    }

    std::cout << "[1] Before hook:\n";
    check(fnAdd(3, 4) == 7, "add(3,4)==7 before hook");
    check(fnSub(10, 3) == 7, "sub(10,3)==7 before hook");

    auto& engine = hookfw::HookEngine::instance();
    engine.setOutputDirectory(DUMP_DIR);
    engine.initialize();

    std::cout << "\n[2] Installing replacement hooks...\n";

    engine.hook("test_target.dll", "test_add")
        .replace(reinterpret_cast<void*>(&hooked_add))
        .install();

    engine.hook("test_target.dll", "test_sub")
        .replace(reinterpret_cast<void*>(&hooked_sub))
        .install();

    bool installed = engine.installAll();
    check(installed, "hooks installed");

    std::cout << "\n[3] After hook (replaced functions):\n";
    int r1 = fnAdd(3, 4);
    check(r1 == 70, "hooked_add(3,4)==70 (replaced: (3+4)*10)");

    int r2 = fnSub(10, 3);
    check(r2 == 999, "hooked_sub(10,3)==999 (replaced: always 999)");

    int r3 = fnAdd(0, 0);
    check(r3 == 0, "hooked_add(0,0)==0");

    int r4 = fnAdd(5, 5);
    check(r4 == 100, "hooked_add(5,5)==100");

    std::cout << "\n[4] Uninstalling hooks...\n";
    engine.uninstallAll();

    int r5 = fnAdd(3, 4);
    check(r5 == 7, "add(3,4)==7 after unhook");

    int r6 = fnSub(10, 3);
    check(r6 == 7, "sub(10,3)==7 after unhook");

    std::cout << "\n=== Results: " << g_testsPassed << " passed, "
              << g_testsFailed << " failed ===\n";

    FreeLibrary(hTarget);
    return g_testsFailed > 0 ? 1 : 0;
}
