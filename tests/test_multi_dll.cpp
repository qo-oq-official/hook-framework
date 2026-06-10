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

typedef int (*FnTestAdd)(int, int);
typedef int (*FnTestSub)(int, int);
typedef int (*FnMathPow)(int, int);
typedef double (*FnMathLerp)(double, double, double);
typedef size_t (*FnStringCount)(const char*, char);

static const char* DUMP_DIR = "test_dump_multi_dll";

int main() {
    std::cout << "=== Multi-DLL Hook Test ===\n";
    std::cout << "Tests: hook functions across multiple DLLs simultaneously\n\n";

    if (fs::exists(DUMP_DIR)) fs::remove_all(DUMP_DIR);

    // Load two separate DLLs
    HMODULE hTestTarget = LoadLibraryA("test_target.dll");
    HMODULE hTargetDll = LoadLibraryA("target_dll.dll");

    check(hTestTarget != nullptr, "test_target.dll loaded");
    check(hTargetDll != nullptr, "target_dll.dll loaded");

    if (!hTestTarget || !hTargetDll) {
        std::cerr << "FATAL: Cannot load DLLs\n";
        return 1;
    }

    // Resolve functions from test_target.dll
    auto fnAdd = reinterpret_cast<FnTestAdd>(GetProcAddress(hTestTarget, "test_add"));
    auto fnSub = reinterpret_cast<FnTestSub>(GetProcAddress(hTestTarget, "test_sub"));

    // Resolve functions from target_dll.dll
    auto fnPow = reinterpret_cast<FnMathPow>(GetProcAddress(hTargetDll, "math_pow"));
    auto fnLerp = reinterpret_cast<FnMathLerp>(GetProcAddress(hTargetDll, "math_lerp"));
    auto fnCount = reinterpret_cast<FnStringCount>(GetProcAddress(hTargetDll, "string_count"));

    check(fnAdd != nullptr, "test_add resolved");
    check(fnSub != nullptr, "test_sub resolved");
    check(fnPow != nullptr, "math_pow resolved");
    check(fnLerp != nullptr, "math_lerp resolved");
    check(fnCount != nullptr, "string_count resolved");

    // --- Initialize framework ---
    auto& engine = hookfw::HookEngine::instance();
    engine.setOutputDirectory(DUMP_DIR);
    engine.initialize();

    // --- Register hooks on BOTH DLLs ---
    std::cout << "\n[1] Registering hooks on test_target.dll ...\n";

    engine.hook("test_target.dll", "test_add")
        .params({{"a", hookfw::ParamType::Int32}, {"b", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::Int32)
        .install();

    engine.hook("test_target.dll", "test_sub")
        .params({{"a", hookfw::ParamType::Int32}, {"b", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::Int32)
        .install();

    std::cout << "[2] Registering hooks on target_dll.dll ...\n";

    engine.hook("target_dll.dll", "math_pow")
        .params({{"base", hookfw::ParamType::Int32}, {"exp", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::Int32)
        .install();

    engine.hook("target_dll.dll", "math_lerp")
        .params({{"a", hookfw::ParamType::Float64}, {"b", hookfw::ParamType::Float64},
                 {"t", hookfw::ParamType::Float64}})
        .returnType(hookfw::ParamType::Float64)
        .install();

    engine.hook("target_dll.dll", "string_count")
        .params({{"str", hookfw::ParamType::CString}, {"ch", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::UInt64)
        .install();

    std::cout << "[3] Installing all hooks (both DLLs in one transaction) ...\n";
    bool installed = engine.installAll();
    check(installed, "all hooks installed in single transaction");

    // --- Call functions from BOTH DLLs ---
    std::cout << "\n[4] Calling hooked functions across both DLLs ...\n";

    int r1 = fnAdd(100, 200);
    check(r1 == 300, "test_target.dll: add(100,200)==300");

    int r2 = fnSub(50, 17);
    check(r2 == 33, "test_target.dll: sub(50,17)==33");

    int r3 = fnPow(2, 8);
    check(r3 == 256, "target_dll.dll: pow(2,8)==256");

    double r4 = fnLerp(0.0, 100.0, 0.25);
    check(r4 == 25.0, "target_dll.dll: lerp(0,100,0.25)==25");

    size_t r5 = fnCount("banana", 'a');
    check(r5 == 3, "target_dll.dll: count('banana','a')==3");

    // --- Verify output from BOTH DLLs ---
    std::cout << "\n[5] Verifying output per DLL ...\n";
    engine.uninstallAll();

    fs::path dumpPath(DUMP_DIR);

    // test_target.dll outputs
    fs::path ttDir = dumpPath / "test_target.dll";
    check(fs::exists(ttDir), "test_target.dll output directory exists");
    check(fs::exists(ttDir / "test_add"), "test_add capture dir");
    check(fs::exists(ttDir / "test_sub"), "test_sub capture dir");

    // target_dll.dll outputs
    fs::path tdDir = dumpPath / "target_dll.dll";
    check(fs::exists(tdDir), "target_dll.dll output directory exists");
    check(fs::exists(tdDir / "math_pow"), "math_pow capture dir");
    check(fs::exists(tdDir / "math_lerp"), "math_lerp capture dir");
    check(fs::exists(tdDir / "string_count"), "string_count capture dir");

    // Count total files
    int totalJson = 0, totalRaw = 0;
    for (auto& entry : fs::recursive_directory_iterator(dumpPath)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".json") totalJson++;
        if (entry.path().extension() == ".raw") totalRaw++;
    }
    std::cout << "  Total: " << totalJson << " JSON, " << totalRaw << " raw\n";
    check(totalJson >= 10, "at least 10 JSON files (5 funcs * entry+exit)");
    check(totalRaw >= 10, "at least 10 raw files");

    // Verify cross-DLL params are correct
    bool addParamsOk = false;
    for (auto& f : fs::directory_iterator(ttDir / "test_add")) {
        if (f.path().extension() == ".json" &&
            f.path().stem().string().find("entry") != std::string::npos) {
            std::ifstream jf(f.path());
            nlohmann::json j; jf >> j;
            if (j.contains("params") && j["params"].size() == 2 &&
                j["params"][0]["value"] == 100 && j["params"][1]["value"] == 200) {
                addParamsOk = true;
            }
            break;
        }
    }
    check(addParamsOk, "test_target.dll: add params (100, 200) captured");

    bool powParamsOk = false;
    for (auto& f : fs::directory_iterator(tdDir / "math_pow")) {
        if (f.path().extension() == ".json" &&
            f.path().stem().string().find("entry") != std::string::npos) {
            std::ifstream jf(f.path());
            nlohmann::json j; jf >> j;
            if (j.contains("params") && j["params"].size() == 2 &&
                j["params"][0]["value"] == 2 && j["params"][1]["value"] == 8) {
                powParamsOk = true;
            }
            break;
        }
    }
    check(powParamsOk, "target_dll.dll: pow params (2, 8) captured");

    bool countParamsOk = false;
    for (auto& f : fs::directory_iterator(tdDir / "string_count")) {
        if (f.path().extension() == ".json" &&
            f.path().stem().string().find("entry") != std::string::npos) {
            std::ifstream jf(f.path());
            nlohmann::json j; jf >> j;
            if (j.contains("params") && j["params"].size() == 2 &&
                j["params"][0]["value"] == "banana") {
                countParamsOk = true;
            }
            break;
        }
    }
    check(countParamsOk, "target_dll.dll: count param 'banana' captured");

    bool lerpRetOk = false;
    for (auto& f : fs::directory_iterator(tdDir / "math_lerp")) {
        if (f.path().extension() == ".json" &&
            f.path().stem().string().find("exit") != std::string::npos) {
            std::ifstream jf(f.path());
            nlohmann::json j; jf >> j;
            if (j.contains("return_value")) {
                lerpRetOk = true;
            }
            break;
        }
    }
    check(lerpRetOk, "target_dll.dll: lerp return value captured");

    // --- Print dump structure ---
    std::cout << "\n  Dump structure:\n";
    for (auto& entry : fs::recursive_directory_iterator(dumpPath)) {
        if (entry.is_directory() && entry.path().parent_path() == dumpPath) {
            std::cout << "  " << entry.path().filename().string() << "/\n";
        }
        if (entry.is_regular_file()) {
            std::cout << "    " << fs::relative(entry.path(), dumpPath).string() << "\n";
        }
    }

    // Cleanup
    if (fs::exists(DUMP_DIR)) {
        try { fs::remove_all(DUMP_DIR); } catch (...) {}
    }

    std::cout << "\n=== Results: " << g_testsPassed << " passed, "
              << g_testsFailed << " failed ===\n";

    FreeLibrary(hTestTarget);
    FreeLibrary(hTargetDll);
    return g_testsFailed > 0 ? 1 : 0;
}
