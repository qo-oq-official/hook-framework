#include <hook_framework/hook_framework.h>
#include <Windows.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

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

typedef int (*FnAdd)(int, int);
typedef int (*FnSub)(int, int);

static const char* DUMP_DIR = "test_dump_rva_display";

int main() {
    std::cout << "=== RVA Display Test ===\n";
    std::cout << "Tests: verify Perfetto trace output contains function names\n\n";

    if (fs::exists(DUMP_DIR)) fs::remove_all(DUMP_DIR);

    HMODULE hTarget = LoadLibraryA("test_target.dll");
    check(hTarget != nullptr, "test_target.dll loaded");
    if (!hTarget) return 1;

    auto fnAdd = reinterpret_cast<FnAdd>(GetProcAddress(hTarget, "test_add"));
    auto fnSub = reinterpret_cast<FnSub>(GetProcAddress(hTarget, "test_sub"));
    check(fnAdd != nullptr, "test_add resolved");
    check(fnSub != nullptr, "test_sub resolved");

    auto& engine = hookfw::HookEngine::instance();
    engine.setOutputDirectory(DUMP_DIR);

    // Register hooks via C++ API
    std::cout << "\n[1] Registering hooks ...\n";
    bool r1 = engine.hook("test_target.dll", "test_add")
        .params({{"a", hookfw::ParamType::Int32}, {"b", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::Int32)
        .install();
    check(r1, "add hook registered");

    bool r2 = engine.hook("test_target.dll", "test_sub")
        .params({{"a", hookfw::ParamType::Int32}, {"b", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::Int32)
        .install();
    check(r2, "sub hook registered");

    bool installed = engine.installAll();
    check(installed, "hooks installed");

    // Call functions
    std::cout << "\n[2] Calling hooked functions ...\n";
    int v1 = fnAdd(7, 8);
    check(v1 == 15, "add(7,8)==15");
    int v2 = fnSub(50, 7);
    check(v2 == 43, "sub(50,7)==43");

    std::cout << "\n[3] Verifying capture output ...\n";
    engine.uninstallAll();

    fs::path dumpPath(DUMP_DIR);
    fs::path addDir = dumpPath / "test_target.dll" / "test_add";
    fs::path subDir = dumpPath / "test_target.dll" / "test_sub";

    check(fs::exists(addDir), "add capture directory exists");
    check(fs::exists(subDir), "sub capture directory exists");

    if (fs::exists(addDir)) {
        // Check entry JSON has correct params
        bool foundEntry = false;
        for (auto& f : fs::directory_iterator(addDir)) {
            if (f.path().extension() == ".json" &&
                f.path().stem().string().find("entry") != std::string::npos) {
                std::ifstream jf(f.path());
                nlohmann::json j;
                jf >> j;
                std::string fname = j.value("function", "");
                std::cout << "  add function name: " << fname << "\n";
                if (j.contains("params") && j["params"].size() == 2 &&
                    j["params"][0]["value"] == 7 && j["params"][1]["value"] == 8) {
                    foundEntry = true;
                }
                break;
            }
        }
        check(foundEntry, "add params captured correctly (7, 8)");
    }

    // Cleanup
    if (fs::exists(DUMP_DIR)) {
        try { fs::remove_all(DUMP_DIR); } catch (...) {}
    }

    std::cout << "\n=== Results: " << g_testsPassed << " passed, "
              << g_testsFailed << " failed ===\n";

    FreeLibrary(hTarget);
    return g_testsFailed > 0 ? 1 : 0;
}
