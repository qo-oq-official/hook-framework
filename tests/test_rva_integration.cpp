#include <Windows.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

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

int main() {
    std::cout << "=== RVA Integration Test ===\n";
    std::cout << "Tests: inject hook DLL via config to verify @RVA_ display in trace.json\n\n";

    fs::path binDir = fs::current_path();
    fs::path injectorPath = binDir / "injector.exe";
    fs::path targetPath = binDir / "target_app.exe";
    fs::path hookDllPath = binDir / "hook_framework.dll";
    fs::path testTargetPath = binDir / "test_target.dll";

    check(fs::exists(injectorPath), "injector.exe exists");
    check(fs::exists(targetPath), "target_app.exe exists");
    check(fs::exists(hookDllPath), "hook_framework.dll exists");
    check(fs::exists(testTargetPath), "test_target.dll exists");

    if (g_testsFailed > 0) return 1;

    // Create work directory
    fs::path workDir = binDir / "test_rva_integration_workdir";
    if (fs::exists(workDir)) fs::remove_all(workDir);
    fs::create_directories(workDir);

    // Copy DLLs to work directory
    fs::path localDll = workDir / "hook_framework.dll";
    fs::path localTarget = workDir / "test_target.dll";
    fs::copy_file(hookDllPath, localDll);
    fs::copy_file(testTargetPath, localTarget);

    // Compute RVAs for verification
    HMODULE hTarget = LoadLibraryW(localTarget.wstring().c_str());
    check(hTarget != nullptr, "test_target.dll loadable");
    if (!hTarget) return 1;

    auto base = reinterpret_cast<uintptr_t>(hTarget);
    auto fnAdd = GetProcAddress(hTarget, "test_add");
    auto fnSub = GetProcAddress(hTarget, "test_sub");
    auto fnMul = GetProcAddress(hTarget, "test_multiply_i64");
    auto rvaAdd = reinterpret_cast<uintptr_t>(fnAdd) - base;
    auto rvaSub = reinterpret_cast<uintptr_t>(fnSub) - base;
    auto rvaMul = reinterpret_cast<uintptr_t>(fnMul) - base;
    FreeLibrary(hTarget);

    std::string expectedAddRva = std::format("@RVA_{:#x}", rvaAdd);
    std::cout << "  Expected RVA suffix: " << expectedAddRva << "\n\n";

    // Write hooks.json with perfetto output (triggers config path with RVA registration)
    nlohmann::json config;
    config["output_dir"] = "dump_data";
    config["output_format"] = "perfetto";
    config["log_level"] = "info";
    config["hooks"] = {
        {{"module","test_target.dll"},{"symbol","test_add"},{"action","capture"},
         {{"params", {{{"name","a"},{"type","int32"}},{{"name","b"},{"type","int32"}}}}},{"return_type","int32"}},
        {{"module","test_target.dll"},{"symbol","test_sub"},{"action","capture"},
         {{"params", {{{"name","a"},{"type","int32"}},{{"name","b"},{"type","int32"}}}}},{"return_type","int32"}},
        {{"module","test_target.dll"},{"symbol","test_multiply_i64"},{"action","capture"},
         {{"params", {{{"name","a"},{"type","int64"}},{{"name","b"},{"type","int64"}}}}},{"return_type","int64"}}
    };

    std::ofstream configFile(workDir / "hooks.json");
    configFile << config.dump(2);
    configFile.close();

    // Copy target_app.exe to work dir
    fs::path localTargetApp = workDir / "target_app.exe";
    fs::copy_file(targetPath, localTargetApp);

    // Launch target_app via injector (target_app waits for stdin Enter)
    std::cout << "[1] Launching target_app.exe via injector ...\n";

    std::string injectCmd = "\"" + injectorPath.string() + "\" --exe \""
                            + localTargetApp.string() + "\" --dll \""
                            + localDll.string() + "\"";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    std::vector<char> cmdBuf(injectCmd.begin(), injectCmd.end());
    cmdBuf.push_back('\0');

    BOOL created = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                                  CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi);
    check(created != 0, "injector process started");

    if (created) {
        // Wait for target_app to start and hooks to install
        std::this_thread::sleep_for(std::chrono::seconds(3));

        // Send Enter to target's console to trigger test functions
        FreeConsole();
        if (AttachConsole(pi.dwProcessId)) {
            HANDLE hInput = CreateFileA("CONIN$", GENERIC_WRITE | GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr, OPEN_EXISTING, 0, nullptr);
            if (hInput != INVALID_HANDLE_VALUE) {
                INPUT_RECORD inputs[4] = {};
                for (int i = 0; i < 4; i += 2) {
                    inputs[i].EventType = KEY_EVENT;
                    inputs[i].Event.KeyEvent.bKeyDown = TRUE;
                    inputs[i].Event.KeyEvent.wRepeatCount = 1;
                    inputs[i].Event.KeyEvent.uChar.AsciiChar = '\r';
                    inputs[i].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;

                    inputs[i+1] = inputs[i];
                    inputs[i+1].Event.KeyEvent.bKeyDown = FALSE;
                }
                DWORD written = 0;
                WriteConsoleInputA(hInput, inputs, 4, &written);
                CloseHandle(hInput);
            }
            FreeConsole();
            AllocConsole();
        }

        WaitForSingleObject(pi.hProcess, 15000);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        check(exitCode == 0, "target_app exited with code 0");
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Verify output
    std::cout << "\n[2] Verifying trace.json ...\n";
    fs::path traceFile = workDir / "dump_data" / "trace.json";
    check(fs::exists(traceFile), "trace.json exists");

    if (fs::exists(traceFile)) {
        std::ifstream f(traceFile);
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        f.close();

        // Verify @RVA_ prefix in event names
        bool hasRva = content.find("@RVA_") != std::string::npos;
        check(hasRva, "trace.json events contain @RVA_ suffix");

        // Verify the correct RVA value appears
        bool correctRva = content.find(expectedAddRva) != std::string::npos;
        check(correctRva, std::format("trace.json contains correct RVA suffix: {}",
                                       expectedAddRva).c_str());

        // Count @RVA_ occurrences (should be >= 6: 3 functions * B+E events)
        size_t rvaCount = 0;
        size_t pos = 0;
        while ((pos = content.find("@RVA_", pos)) != std::string::npos) {
            rvaCount++;
            pos++;
        }
        check(rvaCount >= 6, std::format("at least 6 @RVA_ occurrences (3 funcs * B+E), got {}", rvaCount).c_str());

        // Verify Perfetto-valid JSON
        try {
            nlohmann::json trace = nlohmann::json::parse(content);
            check(trace.contains("traceEvents"), "trace contains traceEvents array");
            check(trace["traceEvents"].size() >= 6, "at least 6 trace events");
        } catch (...) {
            check(false, "trace.json is valid JSON");
        }

        // Print sample event names
        std::cout << "\n  Sample events:\n";
        try {
            nlohmann::json trace = nlohmann::json::parse(content);
            int shown = 0;
            for (const auto& ev : trace["traceEvents"]) {
                if (shown >= 4) break;
                std::string name = ev.value("name", "");
                std::string ph = ev.value("ph", "");
                std::cout << "    " << ph << " " << name << "\n";
                shown++;
            }
        } catch (...) {}
    }

    // Cleanup
    if (fs::exists(workDir)) {
        try { fs::remove_all(workDir); } catch (...) {}
    }

    std::cout << "\n=== Results: " << g_testsPassed << " passed, "
              << g_testsFailed << " failed ===\n";
    return g_testsFailed > 0 ? 1 : 0;
}
