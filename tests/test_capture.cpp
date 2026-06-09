#include <hook_framework/hook_framework.h>
#include <Windows.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cassert>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

static const char* DUMP_DIR = "test_dump_capture";

typedef int (*FnAdd)(int, int);
typedef int (*FnSub)(int, int);
typedef int64_t (*FnMulI64)(int64_t, int64_t);
typedef const char* (*FnEcho)(const char*);
typedef const wchar_t* (*FnConcatW)(const wchar_t*, const wchar_t*);
typedef int (*FnSum4)(int, int, int, int);

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

nlohmann::json readJson(const fs::path& path) {
    std::ifstream f(path);
    if (!f.is_open()) return nullptr;
    nlohmann::json j;
    f >> j;
    return j;
}

int main() {
    std::cout << "=== Hook Framework Capture Test ===\n\n";

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
    auto fnMulI64 = reinterpret_cast<FnMulI64>(GetProcAddress(hTarget, "test_multiply_i64"));
    auto fnEcho = reinterpret_cast<FnEcho>(GetProcAddress(hTarget, "test_echo_str"));
    auto fnConcatW = reinterpret_cast<FnConcatW>(GetProcAddress(hTarget, "test_concat_w"));
    auto fnSum4 = reinterpret_cast<FnSum4>(GetProcAddress(hTarget, "test_sum4"));

    if (!fnAdd || !fnSub || !fnMulI64 || !fnEcho || !fnConcatW || !fnSum4) {
        std::cerr << "FATAL: Cannot resolve test functions\n";
        return 1;
    }

    auto& engine = hookfw::HookEngine::instance();
    engine.setOutputDirectory(DUMP_DIR);
    engine.initialize();

    std::cout << "[1] Registering hooks...\n";

    engine.hook("test_target.dll", "test_add")
        .params({{"a", hookfw::ParamType::Int32}, {"b", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::Int32)
        .install();

    engine.hook("test_target.dll", "test_sub")
        .params({{"a", hookfw::ParamType::Int32}, {"b", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::Int32)
        .install();

    engine.hook("test_target.dll", "test_multiply_i64")
        .params({{"a", hookfw::ParamType::Int64}, {"b", hookfw::ParamType::Int64}})
        .returnType(hookfw::ParamType::Int64)
        .install();

    engine.hook("test_target.dll", "test_echo_str")
        .params({{"msg", hookfw::ParamType::CString}})
        .returnType(hookfw::ParamType::Pointer)
        .install();

    engine.hook("test_target.dll", "test_concat_w")
        .params({{"a", hookfw::ParamType::WString}, {"b", hookfw::ParamType::WString}})
        .returnType(hookfw::ParamType::Pointer)
        .install();

    engine.hook("test_target.dll", "test_sum4")
        .params({{"a", hookfw::ParamType::Int32}, {"b", hookfw::ParamType::Int32},
                 {"c", hookfw::ParamType::Int32}, {"d", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::Int32)
        .install();

    std::cout << "[2] Installing hooks...\n";
    bool installed = engine.installAll();
    check(installed, "hooks installed");

    std::cout << "[3] Calling hooked functions...\n";

    int r1 = fnAdd(10, 20);
    check(r1 == 30, "add(10,20)==30");

    int r2 = fnSub(100, 37);
    check(r2 == 63, "sub(100,37)==63");

    int64_t r3 = fnMulI64(1000000LL, 999999LL);
    check(r3 == 999999000000LL, "multiply_i64(1000000,999999)");

    const char* r4 = fnEcho("hello world");
    check(r4 != nullptr && strcmp(r4, "hello world") == 0, "echo_str('hello world')");

    const wchar_t* r5 = fnConcatW(L"foo", L"bar");
    check(r5 != nullptr && wcscmp(r5, L"foobar") == 0, "concat_w('foo','bar')");

    int r6 = fnSum4(1, 2, 3, 4);
    check(r6 == 10, "sum4(1,2,3,4)==10");

    std::cout << "\n[4] Verifying output files...\n";

    engine.uninstallAll();
    Sleep(100);

    auto addDir = fs::path(DUMP_DIR) / "test_target.dll" / "test_add";
    auto subDir = fs::path(DUMP_DIR) / "test_target.dll" / "test_sub";
    auto mulDir = fs::path(DUMP_DIR) / "test_target.dll" / "test_multiply_i64";
    auto echoDir = fs::path(DUMP_DIR) / "test_target.dll" / "test_echo_str";
    auto concatDir = fs::path(DUMP_DIR) / "test_target.dll" / "test_concat_w";

    check(fs::exists(addDir), "add_numbers dump dir exists");
    check(fs::exists(subDir), "sub dump dir exists");
    check(fs::exists(mulDir), "multiply_i64 dump dir exists");

    int fileCount = 0;
    for (auto& entry : fs::recursive_directory_iterator(DUMP_DIR)) {
        if (entry.is_regular_file()) fileCount++;
    }
    check(fileCount >= 12, "at least 12 output files (6 hooks * entry+exit)");

    bool foundEntryJson = false;
    for (auto& f : fs::directory_iterator(addDir)) {
        if (f.path().extension() == ".json" && f.path().stem().string().find("entry") != std::string::npos) {
            auto j = readJson(f.path());
            if (!j.is_null() && j.contains("params")) {
                auto& params = j["params"];
                if (params.size() == 2 &&
                    params[0]["value"] == 10 &&
                    params[1]["value"] == 20) {
                    foundEntryJson = true;
                }
            }
            break;
        }
    }
    check(foundEntryJson, "add entry json has correct params (10, 20)");

    bool foundExitJson = false;
    for (auto& f : fs::directory_iterator(addDir)) {
        if (f.path().extension() == ".json" && f.path().stem().string().find("exit") != std::string::npos) {
            auto j = readJson(f.path());
            if (!j.is_null() && j.contains("return_value")) {
                if (j["return_value"]["value"] == 30) {
                    foundExitJson = true;
                }
            }
            break;
        }
    }
    check(foundExitJson, "add exit json has return_value=30");

    bool foundConcatEntry = false;
    for (auto& f : fs::directory_iterator(concatDir)) {
        if (f.path().extension() == ".json" && f.path().stem().string().find("entry") != std::string::npos) {
            auto j = readJson(f.path());
            if (!j.is_null() && j.contains("params")) {
                auto& params = j["params"];
                if (params.size() == 2 &&
                    params[0]["value"] == "foo" &&
                    params[1]["value"] == "bar") {
                    foundConcatEntry = true;
                }
            }
            break;
        }
    }
    check(foundConcatEntry, "concat_w entry json has wstring params ('foo','bar')");

    bool hasRawFiles = false;
    for (auto& f : fs::directory_iterator(addDir)) {
        if (f.path().extension() == ".raw" && f.file_size() > 0) {
            hasRawFiles = true;
            break;
        }
    }
    check(hasRawFiles, "raw dump files exist and are non-empty");

    std::cout << "\n=== Results: " << g_testsPassed << " passed, "
              << g_testsFailed << " failed ===\n";

    FreeLibrary(hTarget);
    return g_testsFailed > 0 ? 1 : 0;
}
