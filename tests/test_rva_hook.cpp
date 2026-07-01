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
typedef int64_t (*FnMulI64)(int64_t, int64_t);

static const char* DUMP_DIR = "test_dump_rva_hook";

static uint64_t getFuncRva(HMODULE hMod, const char* funcName) {
    auto addr = reinterpret_cast<uintptr_t>(GetProcAddress(hMod, funcName));
    auto base = reinterpret_cast<uintptr_t>(hMod);
    return addr - base;
}

static DWORD getModuleImageSize(HMODULE hMod) {
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(hMod);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const uint8_t*>(hMod) + dos->e_lfanew);
    return nt->OptionalHeader.SizeOfImage;
}

int main() {
    std::cout << "=== RVA Hook Test ===\n";
    std::cout << "Tests: valid RVA, invalid RVA, invalid module, partial failure\n\n";

    if (fs::exists(DUMP_DIR)) fs::remove_all(DUMP_DIR);

    HMODULE hTarget = LoadLibraryA("test_target.dll");
    check(hTarget != nullptr, "test_target.dll loaded");
    if (!hTarget) return 1;

    auto fnAdd = reinterpret_cast<FnTestAdd>(GetProcAddress(hTarget, "test_add"));
    auto fnSub = reinterpret_cast<FnTestSub>(GetProcAddress(hTarget, "test_sub"));
    auto fnMul = reinterpret_cast<FnMulI64>(GetProcAddress(hTarget, "test_multiply_i64"));

    check(fnAdd != nullptr, "test_add resolved");
    check(fnSub != nullptr, "test_sub resolved");
    check(fnMul != nullptr, "test_multiply_i64 resolved");

    uint64_t rvaAdd = getFuncRva(hTarget, "test_add");
    uint64_t rvaSub = getFuncRva(hTarget, "test_sub");
    uint64_t rvaMul = getFuncRva(hTarget, "test_multiply_i64");
    DWORD imageSize = getModuleImageSize(hTarget);

    std::cout << "\n  test_target.dll image size: 0x" << std::hex << imageSize << std::dec << "\n";
    std::cout << "  test_add RVA:         0x" << std::hex << rvaAdd << "\n";
    std::cout << "  test_sub RVA:         0x" << std::hex << rvaSub << "\n";
    std::cout << "  test_multiply_i64 RVA:0x" << std::hex << rvaMul << std::dec << "\n\n";

    auto& engine = hookfw::HookEngine::instance();
    engine.setOutputDirectory(DUMP_DIR);
    engine.initialize();

    // ============================================================
    // Scenario 1: Valid RVA hooks
    // ============================================================
    std::cout << "[1] Valid RVA hooks ...\n";

    bool r1 = engine.hookByRva("test_target.dll", rvaAdd)
        .params({{"a", hookfw::ParamType::Int32}, {"b", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::Int32)
        .install();
    check(r1, "hook test_add by RVA registered");

    bool r2 = engine.hookByRva("test_target.dll", rvaSub)
        .params({{"a", hookfw::ParamType::Int32}, {"b", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::Int32)
        .install();
    check(r2, "hook test_sub by RVA registered");

    // ============================================================
    // Scenario 2: Invalid RVA (exceeds image size) - should fail gracefully
    // ============================================================
    std::cout << "\n[2] Invalid RVA (exceeds image size) ...\n";

    uint64_t badRva = static_cast<uint64_t>(imageSize) + 0x100000;
    bool r3 = engine.hookByRva("test_target.dll", badRva)
        .params({{"x", hookfw::ParamType::Int32}})
        .install();
    check(!r3, "hook with RVA beyond image size correctly rejected");

    uint64_t hugeRva = 0x2EB9A2E0;
    bool r4 = engine.hookByRva("test_target.dll", hugeRva)
        .params({{"x", hookfw::ParamType::Int32}})
        .install();
    check(!r4, "hook with absurdly large RVA (absolute addr) correctly rejected");

    // ============================================================
    // Scenario 3: Invalid module name - should fail gracefully
    // ============================================================
    std::cout << "\n[3] Invalid module name ...\n";

    bool r5 = engine.hookByRva("nonexistent_module.dll", 0x1000)
        .params({{"x", hookfw::ParamType::Int32}})
        .install();
    check(!r5, "hook on nonexistent module correctly rejected");

    bool r6 = engine.hook("test_target.dll", "nonexistent_function_xyz")
        .params({{"x", hookfw::ParamType::Int32}})
        .install();
    check(!r6, "hook on nonexistent symbol correctly rejected");

    // ============================================================
    // Scenario 4: Install - valid hooks succeed despite earlier failures
    // ============================================================
    std::cout << "\n[4] Installing hooks (valid ones should succeed) ...\n";

    bool installed = engine.installAll();
    check(installed, "installAll succeeds (valid hooks installed, invalid skipped)");

    // ============================================================
    // Scenario 5: Call hooked functions - verify they work
    // ============================================================
    std::cout << "\n[5] Calling RVA-hooked functions ...\n";

    int ra = fnAdd(7, 8);
    check(ra == 15, "test_add(7,8)==15 via RVA hook");

    int rb = fnSub(100, 42);
    check(rb == 58, "test_sub(100,42)==58 via RVA hook");

    // ============================================================
    // Scenario 6: Verify output
    // ============================================================
    std::cout << "\n[6] Verifying captured output ...\n";
    engine.uninstallAll();

    fs::path dumpPath(DUMP_DIR);
    fs::path addDir = dumpPath / "test_target.dll" / ("RVA_0x" + std::format("{:08x}", rvaAdd));
    fs::path subDir = dumpPath / "test_target.dll" / ("RVA_0x" + std::format("{:08x}", rvaSub));

    check(fs::exists(dumpPath), "dump_data directory created");
    check(fs::exists(addDir), "RVA add function capture directory exists");
    check(fs::exists(subDir), "RVA sub function capture directory exists");

    if (fs::exists(addDir)) {
        bool foundEntry = false;
        for (auto& f : fs::directory_iterator(addDir)) {
            if (f.path().extension() == ".json" &&
                f.path().stem().string().find("entry") != std::string::npos) {
                std::ifstream jf(f.path());
                nlohmann::json j; jf >> j;
                if (j.contains("args") && j["args"].size() == 2 &&
                    j["args"][0]["value"] == 7 && j["args"][1]["value"] == 8) {
                    foundEntry = true;
                }
                break;
            }
        }
        check(foundEntry, "RVA hook captured correct params (7, 8)");

        bool foundExit = false;
        for (auto& f : fs::directory_iterator(addDir)) {
            if (f.path().extension() == ".json" &&
                f.path().stem().string().find("exit") != std::string::npos) {
                std::ifstream jf(f.path());
                nlohmann::json j; jf >> j;
                if (j.contains("return") && j["return"]["value"] == 15) {
                    foundExit = true;
                }
                break;
            }
        }
        check(foundExit, "RVA hook captured correct return value (15)");
    }

    int totalFiles = 0;
    if (fs::exists(dumpPath)) {
        for (auto& e : fs::recursive_directory_iterator(dumpPath)) {
            if (e.is_regular_file()) totalFiles++;
        }
    }
    std::cout << "  Total output files: " << totalFiles << "\n";
    check(totalFiles >= 8, "at least 8 output files (2 valid hooks * 2 calls * entry+exit)");

    // Cleanup
    if (fs::exists(DUMP_DIR)) {
        try { fs::remove_all(DUMP_DIR); } catch (...) {}
    }

    std::cout << "\n=== Results: " << g_testsPassed << " passed, "
              << g_testsFailed << " failed ===\n";

    FreeLibrary(hTarget);
    return g_testsFailed > 0 ? 1 : 0;
}
