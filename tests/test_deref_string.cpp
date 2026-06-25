#include <hook_framework/hook_framework.h>
#include <Windows.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <cstring>

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

typedef int (*FnReadStr)(const char**);
typedef int (*FnReadWstr)(const wchar_t**);

static const char* DUMP_DIR = "test_dump_deref_str";

nlohmann::json findExitJson(const fs::path& dir) {
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
    std::cout << "=== Deref String Types Test ===\n";
    std::cout << "Tests: verify CStringRef/WStringRef deref in output_params\n\n";

    if (fs::exists(DUMP_DIR)) fs::remove_all(DUMP_DIR);

    HMODULE hTarget = LoadLibraryA("test_target.dll");
    check(hTarget != nullptr, "test_target.dll loaded");
    if (!hTarget) return 1;
    std::cout << "[0] DLL loaded\n" << std::flush;

    auto fnReadStr  = reinterpret_cast<FnReadStr>(GetProcAddress(hTarget, "test_read_str"));
    auto fnReadWstr = reinterpret_cast<FnReadWstr>(GetProcAddress(hTarget, "test_read_wstr"));
    check(fnReadStr != nullptr, "test_read_str resolved");
    check(fnReadWstr != nullptr, "test_read_wstr resolved");
    std::cout << "[0b] functions resolved\n" << std::flush;

    auto& engine = hookfw::HookEngine::instance();
    engine.setOutputDirectory(DUMP_DIR);
    engine.initialize();

    // CStringRef deref
    std::cout << "\n[1] CStringRef deref test ...\n" << std::flush;
    hookfw::ParamDescriptor pStrOut;
    pStrOut.name = "pOut";
    pStrOut.type = hookfw::ParamType::Pointer;
    pStrOut.derefType = hookfw::ParamType::CStringRef;
    pStrOut.hasDeref = true;
    pStrOut.captureOnExit = true;
    engine.hook("test_target.dll", "test_read_str")
        .params({pStrOut})
        .returnType(hookfw::ParamType::Int32)
        .install();

    // WStringRef deref
    std::cout << "\n[2] WStringRef deref test ...\n" << std::flush;
    hookfw::ParamDescriptor pWstrOut;
    pWstrOut.name = "pOut";
    pWstrOut.type = hookfw::ParamType::Pointer;
    pWstrOut.derefType = hookfw::ParamType::WStringRef;
    pWstrOut.hasDeref = true;
    pWstrOut.captureOnExit = true;
    engine.hook("test_target.dll", "test_read_wstr")
        .params({pWstrOut})
        .returnType(hookfw::ParamType::Int32)
        .install();

    std::cout << "[3] Installing hooks ...\n";
    bool installed = engine.installAll();
    check(installed, "all hooks installed");

    // Call functions
    std::cout << "\n[4] Calling functions ...\n";
    const char* strOut = nullptr;
    int r1 = fnReadStr(&strOut);
    check(r1 == 1 && strOut && strcmp(strOut, "hello_deref") == 0,
          "test_read_str wrote 'hello_deref'");

    const wchar_t* wstrOut = nullptr;
    int r2 = fnReadWstr(&wstrOut);
    check(r2 == 1 && wstrOut && wcscmp(wstrOut, L"hello_wide_deref") == 0,
          "test_read_wstr wrote 'hello_wide_deref'");

    engine.uninstallAll();

    // Verify captured output
    std::cout << "\n[5] Verifying deref in captured data ...\n";

    auto strDir = fs::path(DUMP_DIR) / "test_target.dll" / "test_read_str";
    check(fs::exists(strDir), "test_read_str capture dir exists");
    if (fs::exists(strDir)) {
        auto j = findExitJson(strDir);
        bool hasDeref = !j.is_null() && j.contains("output_params");
        if (hasDeref) {
            for (const auto& op : j["output_params"]) {
                if (op.contains("deref_value") && op["deref_value"].is_string()) {
                    hasDeref = (op["deref_value"].get<std::string>() == "hello_deref");
                    break;
                }
            }
        }
        check(hasDeref, "CStringRef deref_value == 'hello_deref'");
        if (!j.is_null()) std::cout << "    exit.json: " << j.dump() << "\n";
    }

    auto wstrDir = fs::path(DUMP_DIR) / "test_target.dll" / "test_read_wstr";
    check(fs::exists(wstrDir), "test_read_wstr capture dir exists");
    if (fs::exists(wstrDir)) {
        auto j = findExitJson(wstrDir);
        bool hasDeref = !j.is_null() && j.contains("output_params");
        if (hasDeref) {
            for (const auto& op : j["output_params"]) {
                if (op.contains("deref_value") && op["deref_value"].is_string() &&
                    op["deref_value"].get<std::string>().find("hello_wide") != std::string::npos) {
                    break;
                }
                hasDeref = false;
            }
        }
        check(hasDeref, "WStringRef deref_value contains 'hello_wide'");
        if (!j.is_null()) std::cout << "    exit.json: " << j.dump() << "\n";
    }

    if (fs::exists(DUMP_DIR)) {
        try { fs::remove_all(DUMP_DIR); } catch (...) {}
    }

    std::cout << "\n=== Results: " << g_testsPassed << " passed, "
              << g_testsFailed << " failed ===\n";

    FreeLibrary(hTarget);
    return g_testsFailed > 0 ? 1 : 0;
}
