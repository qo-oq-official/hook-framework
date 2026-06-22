#include "hook_framework/hook_framework.h"
#include <Windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while(0)

typedef int (*AddFunc)(int, int);
typedef int (*SubFunc)(int, int);

int main() {
    printf("=== Perfetto Params Integration Test ===\n");
    printf("Tests: config-driven params + demangled names\n\n");

    HMODULE hTarget = LoadLibraryA("test_target.dll");
    CHECK(hTarget != nullptr, "test_target.dll loaded");
    if (!hTarget) return 1;

    auto test_add = reinterpret_cast<AddFunc>(GetProcAddress(hTarget, "test_add"));
    auto test_sub = reinterpret_cast<SubFunc>(GetProcAddress(hTarget, "test_sub"));
    CHECK(test_add != nullptr, "test_add resolved");
    CHECK(test_sub != nullptr, "test_sub resolved");

    // Read and parse trace.json after framework shutdown
    std::string outDir = "dump_data_perfetto";
    std::filesystem::create_directories(outDir);

    printf("\n[1] Calling test_add(7,8) before hooks...\n");
    int pre = test_add(7, 8);
    CHECK(pre == 15, "test_add works before hooks");

    // ===== Set up hook via C++ API =====
    printf("\n[2] Setting up hook with typed params...\n");
    auto& engine = hookfw::HookEngine::instance();
    engine.initialize();
    engine.setOutputDirectory(outDir);

    bool ok = engine.hook("test_target.dll", "test_add")
        .params({{"a", hookfw::ParamType::Int32}, {"b", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::Int32)
        .install();
    CHECK(ok, "typed hook installed");

    CHECK(engine.installAll(), "installAll succeeded");

    printf("\n[3] Calling test_add(7,8) with hooks active...\n");
    int r1 = test_add(7, 8);
    CHECK(r1 == 15, "test_add(7,8) == 15");

    engine.shutdown();
    Sleep(200);

    // ===== Verify capture output =====
    printf("\n[4] Verifying capture output (JSON per-call files)...\n");
    bool foundEntry = false, foundExit = false;
    for (const auto& e : std::filesystem::recursive_directory_iterator(outDir)) {
        auto path = e.path().string();
        if (path.find("test_add") != std::string::npos) {
            std::string name = e.path().filename().string();
            if (name.find("entry.json") != std::string::npos) {
                foundEntry = true;
                std::ifstream f(path);
                std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                bool hasA = content.find("\"name\": \"a\"") != std::string::npos ||
                            content.find("\"name\":\"a\"") != std::string::npos;
                bool hasB = content.find("\"name\": \"b\"") != std::string::npos ||
                            content.find("\"name\":\"b\"") != std::string::npos;
                bool hasVal7 = content.find("\"value\": 7") != std::string::npos ||
                               content.find("\"value\":7") != std::string::npos;
                bool hasVal8 = content.find("\"value\": 8") != std::string::npos ||
                               content.find("\"value\":8") != std::string::npos;
                CHECK(hasA, "capture entry: param name 'a' present");
                CHECK(hasB, "capture entry: param name 'b' present");
                CHECK(hasVal7, "capture entry: value 7 present");
                CHECK(hasVal8, "capture entry: value 8 present");
            }
            if (name.find("exit.json") != std::string::npos) {
                foundExit = true;
                std::ifstream f(path);
                std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                bool hasRet = content.find("\"value\": 15") != std::string::npos ||
                              content.find("\"value\":15") != std::string::npos;
                CHECK(hasRet, "capture exit: return value 15 present");
            }
        }
    }
    CHECK(foundEntry, "entry.json found");
    CHECK(foundExit, "exit.json found");

    std::filesystem::remove_all(outDir);

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail;
}
