#include <hook_framework/hook_framework.h>
#include <Windows.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cmath>
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

typedef int (*FnReadInt)(int*);
typedef int (*FnReadDouble)(double*);
typedef int (*FnReadPair)(int*, int*);
typedef int (*FnReadInt64)(int64_t*);
typedef int (*FnReadFloat)(float*);

static const char* DUMP_DIR = "test_dump_deref";

nlohmann::json findExitJson(const fs::path& dir, const std::string& contains) {
    for (auto& f : fs::directory_iterator(dir)) {
        if (f.path().extension() == ".json" &&
            f.path().stem().string().find("exit") != std::string::npos) {
            std::ifstream jf(f.path());
            nlohmann::json j;
            jf >> j;
            return j;
        }
    }
    return nullptr;
}

int main() {
    std::cout << "=== Deref Output Params Test ===\n";
    std::cout << "Tests: verify dereferenced reference params are captured at exit\n\n";

    if (fs::exists(DUMP_DIR)) fs::remove_all(DUMP_DIR);

    HMODULE hTarget = LoadLibraryA("test_target.dll");
    check(hTarget != nullptr, "test_target.dll loaded");
    if (!hTarget) return 1;

    auto fnReadInt = reinterpret_cast<FnReadInt>(GetProcAddress(hTarget, "test_read_int"));
    auto fnReadDouble = reinterpret_cast<FnReadDouble>(GetProcAddress(hTarget, "test_read_double"));
    auto fnReadPair = reinterpret_cast<FnReadPair>(GetProcAddress(hTarget, "test_read_pair"));
    auto fnReadInt64 = reinterpret_cast<FnReadInt64>(GetProcAddress(hTarget, "test_read_int64"));
    auto fnReadFloat = reinterpret_cast<FnReadFloat>(GetProcAddress(hTarget, "test_read_float"));

    check(fnReadInt != nullptr, "test_read_int resolved");
    check(fnReadDouble != nullptr, "test_read_double resolved");
    check(fnReadPair != nullptr, "test_read_pair resolved");
    check(fnReadInt64 != nullptr, "test_read_int64 resolved");
    check(fnReadFloat != nullptr, "test_read_float resolved");

    auto& engine = hookfw::HookEngine::instance();
    engine.setOutputDirectory(DUMP_DIR);
    engine.initialize();

    // --- Register hooks with deref_type ---
    std::cout << "\n[1] Registering hooks with deref_type ...\n";

    hookfw::ParamDescriptor pOutInt{"pOut", hookfw::ParamType::Pointer, 0, hookfw::ParamType::Int32, true};
    hookfw::ParamDescriptor pOutDouble{"pOut", hookfw::ParamType::Pointer, 0, hookfw::ParamType::Float64, true};
    hookfw::ParamDescriptor pOutA{"pOutA", hookfw::ParamType::Pointer, 0, hookfw::ParamType::Int32, true};
    hookfw::ParamDescriptor pOutB{"pOutB", hookfw::ParamType::Pointer, 0, hookfw::ParamType::Int32, true};
    hookfw::ParamDescriptor pOutI64{"pOut", hookfw::ParamType::Pointer, 0, hookfw::ParamType::Int64, true};
    hookfw::ParamDescriptor pOutFloat{"pOut", hookfw::ParamType::Pointer, 0, hookfw::ParamType::Float32, true};

    engine.hook("test_target.dll", "test_read_int")
        .params({pOutInt})
        .returnType(hookfw::ParamType::Int32)
        .install();

    engine.hook("test_target.dll", "test_read_double")
        .params({pOutDouble})
        .returnType(hookfw::ParamType::Int32)
        .install();

    engine.hook("test_target.dll", "test_read_pair")
        .params({pOutA, pOutB})
        .returnType(hookfw::ParamType::Int32)
        .install();

    engine.hook("test_target.dll", "test_read_int64")
        .params({pOutI64})
        .returnType(hookfw::ParamType::Int32)
        .install();

    engine.hook("test_target.dll", "test_read_float")
        .params({pOutFloat})
        .returnType(hookfw::ParamType::Int32)
        .install();

    std::cout << "[2] Installing hooks ...\n";
    bool installed = engine.installAll();
    check(installed, "all hooks installed");

    // --- Call functions ---
    std::cout << "\n[3] Calling functions with output params ...\n";

    int intVal = 0;
    int r1 = fnReadInt(&intVal);
    check(r1 == 1 && intVal == 12345, "test_read_int wrote 12345");

    double dblVal = 0.0;
    int r2 = fnReadDouble(&dblVal);
    check(r2 == 1 && std::abs(dblVal - 3.14159265) < 0.0001, "test_read_double wrote 3.14159");

    int pairA = 0, pairB = 0;
    int r3 = fnReadPair(&pairA, &pairB);
    check(r3 == 2 && pairA == 100 && pairB == 200, "test_read_pair wrote 100, 200");

    int64_t i64Val = 0;
    int r4 = fnReadInt64(&i64Val);
    check(r4 == 1 && i64Val == 9876543210LL, "test_read_int64 wrote 9876543210");

    float fltVal = 0.0f;
    int r5 = fnReadFloat(&fltVal);
    check(r5 == 1 && std::abs(fltVal - 2.718f) < 0.001f, "test_read_float wrote 2.718");

    // --- Verify captured output ---
    std::cout << "\n[4] Verifying deref output in captured data ...\n";
    engine.uninstallAll();

    fs::path dumpPath(DUMP_DIR);

    // test_read_int: exit should have output_params with deref_value = 12345
    auto intDir = dumpPath / "test_target.dll" / "test_read_int";
    check(fs::exists(intDir), "test_read_int capture dir exists");
    if (fs::exists(intDir)) {
        auto j = findExitJson(intDir, "exit");
        bool hasDeref = !j.is_null() && j.contains("args") &&
                        j["args"].size() >= 1 &&
                        j["args"][0].contains("deref") &&
                        j["args"][0]["deref"] == 12345;
        check(hasDeref, "test_read_int exit: deref_value == 12345");
        if (!j.is_null() && j.contains("args")) {
            std::cout << "    args: " << j["args"].dump() << "\n";
        }
    }

    // test_read_double: exit should have deref_value close to 3.14159
    auto dblDir = dumpPath / "test_target.dll" / "test_read_double";
    check(fs::exists(dblDir), "test_read_double capture dir exists");
    if (fs::exists(dblDir)) {
        auto j = findExitJson(dblDir, "exit");
        bool hasDeref = false;
        if (!j.is_null() && j.contains("args") &&
            j["args"].size() >= 1 &&
            j["args"][0].contains("deref")) {
            double captured = j["args"][0]["deref"].get<double>();
            hasDeref = std::abs(captured - 3.14159265) < 0.001;
        }
        check(hasDeref, "test_read_double exit: deref_value ~= 3.14159");
        if (!j.is_null() && j.contains("args")) {
            std::cout << "    output_params: " << j["args"].dump() << "\n";
        }
    }

    // test_read_pair: exit should have two output_params
    auto pairDir = dumpPath / "test_target.dll" / "test_read_pair";
    check(fs::exists(pairDir), "test_read_pair capture dir exists");
    if (fs::exists(pairDir)) {
        auto j = findExitJson(pairDir, "exit");
        bool hasDeref = false;
        if (!j.is_null() && j.contains("args") &&
            j["args"].size() >= 2) {
            int a = j["args"][0].value("deref", 0);
            int b = j["args"][1].value("deref", 0);
            hasDeref = (a == 100 && b == 200);
        }
        check(hasDeref, "test_read_pair exit: pOutA=100, pOutB=200");
        if (!j.is_null() && j.contains("args")) {
            std::cout << "    output_params: " << j["args"].dump() << "\n";
        }
    }

    // test_read_int64: exit deref_value == 9876543210
    auto i64Dir = dumpPath / "test_target.dll" / "test_read_int64";
    check(fs::exists(i64Dir), "test_read_int64 capture dir exists");
    if (fs::exists(i64Dir)) {
        auto j = findExitJson(i64Dir, "exit");
        bool hasDeref = false;
        if (!j.is_null() && j.contains("args") &&
            j["args"].size() >= 1 &&
            j["args"][0].contains("deref")) {
            int64_t captured = j["args"][0]["deref"].get<int64_t>();
            hasDeref = (captured == 9876543210LL);
        }
        check(hasDeref, "test_read_int64 exit: deref_value == 9876543210");
        if (!j.is_null() && j.contains("args")) {
            std::cout << "    output_params: " << j["args"].dump() << "\n";
        }
    }

    // test_read_float: exit deref_value ~= 2.718
    auto fltDir = dumpPath / "test_target.dll" / "test_read_float";
    check(fs::exists(fltDir), "test_read_float capture dir exists");
    if (fs::exists(fltDir)) {
        auto j = findExitJson(fltDir, "exit");
        bool hasDeref = false;
        if (!j.is_null() && j.contains("args") &&
            j["args"].size() >= 1 &&
            j["args"][0].contains("deref")) {
            float captured = j["args"][0]["deref"].get<float>();
            hasDeref = std::abs(captured - 2.718f) < 0.01f;
        }
        check(hasDeref, "test_read_float exit: deref_value ~= 2.718");
        if (!j.is_null() && j.contains("args")) {
            std::cout << "    output_params: " << j["args"].dump() << "\n";
        }
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
