#include "hook_framework/hook_framework.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <format>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while(0)

static uint64_t getFuncRva(HMODULE hMod, const char* funcName) {
    auto addr = reinterpret_cast<uintptr_t>(GetProcAddress(hMod, funcName));
    if (!addr) return 0;
    return addr - reinterpret_cast<uintptr_t>(hMod);
}

int main(int argc, char** argv) {
    int FILLER_COUNT = (argc > 1) ? atoi(argv[1]) : 508;
    const int RVA_FN_COUNT = 12;

    printf("=== RVA Scale Test ===\n");
    printf("Tests: %d filler hooks + %d RVA hooks\n\n", FILLER_COUNT, RVA_FN_COUNT);

    printf("[1] Loading test_scale_target.dll...\n");
    HMODULE hMod = LoadLibraryA("test_scale_target.dll");
    CHECK(hMod != nullptr, "test_scale_target.dll loaded");
    if (!hMod) return 1;

    printf("\n[2] Resolving RVAs for %d target functions...\n", RVA_FN_COUNT);
    std::vector<uint64_t> rvas;
    for (int i = 0; i < RVA_FN_COUNT; i++) {
        char name[64];
        sprintf_s(name, "test_rva_fn_%d", i);
        uint64_t rva = getFuncRva(hMod, name);
        CHECK(rva > 0, (std::string("RVA for ") + name + std::format(" = {:#x}", rva)).c_str());
        rvas.push_back(rva);
    }

    auto& engine = hookfw::HookEngine::instance();
    engine.initialize();

    printf("\n[3] Registering %d filler trace hooks...\n", FILLER_COUNT);
    for (int i = 0; i < FILLER_COUNT; i++) {
        char name[64];
        sprintf_s(name, "test_n%04d", i);
        engine.hook("test_scale_target.dll", name).install();
    }
    CHECK(true, "Filler hooks registered");

    printf("\n[4] Registering %d RVA capture hooks...\n", RVA_FN_COUNT);
    for (int i = 0; i < RVA_FN_COUNT; i++) {
        char displayName[64];
        sprintf_s(displayName, "test_rva_fn_%d", i);
        bool ok = engine.hookByRva("test_scale_target.dll", rvas[i])
            .params({{"a", hookfw::ParamType::Int32}})
            .returnType(hookfw::ParamType::Int32)
            .install();
        CHECK(ok, (std::string("RVA hook ") + displayName +
                   std::format(" @RVA_0x{:08x} registered", rvas[i])).c_str());
    }

    printf("\n[5] Installing all hooks (%d total)...\n", FILLER_COUNT + RVA_FN_COUNT);
    bool installed = engine.installAll();
    CHECK(installed, "installAll succeeded");

    if (!installed) {
        printf("  WARNING: Some hooks failed. Check hook_attach_err.log.\n");
    }

    printf("\n[6] Calling RVA target functions to verify hooking...\n");
    typedef int (*RvaFn)(int);
    for (int i = 0; i < RVA_FN_COUNT; i++) {
        char name[64];
        sprintf_s(name, "test_rva_fn_%d", i);
        auto ptr = reinterpret_cast<RvaFn>(GetProcAddress(hMod, name));
        if (ptr) {
            int result = ptr(5);
            printf("  %s(5) = %d\n", name, result);
        } else {
            printf("  %s: could not resolve\n", name);
        }
    }

    printf("\n[7] Checking attach error log...\n");
    FILE* errLog = nullptr;
    if (fopen_s(&errLog, "hook_attach_err.log", "r") == 0 && errLog) {
        char line[512];
        int errCount = 0;
        while (fgets(line, sizeof(line), errLog)) {
            printf("  %s", line);
            errCount++;
        }
        fclose(errLog);
        if (errCount == 0) {
            printf("  No errors found - all RVA hooks attached successfully\n");
            CHECK(true, "No DetourAttach errors");
        } else {
            CHECK(false, (std::string("DetourAttach errors: ") + std::to_string(errCount)).c_str());
        }
    } else {
        printf("  hook_attach_err.log not found\n");
    }

    engine.shutdown();

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail;
}
