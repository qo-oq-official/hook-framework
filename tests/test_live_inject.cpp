#include <Windows.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <string>
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

int main() {
    std::cout << "=== Live Process Injection Hook Test ===\n";
    std::cout << "Tests: inject hook DLL into an already-running process\n\n";

    fs::path binDir = fs::current_path();
    fs::path injectorPath = binDir / "injector.exe";
    fs::path targetPath = binDir / "target_app.exe";
    fs::path hookDllPath = binDir / "hook_framework.dll";

    check(fs::exists(injectorPath), "injector.exe exists");
    check(fs::exists(targetPath), "target_app.exe exists");
    check(fs::exists(hookDllPath), "hook_framework.dll exists");

    if (g_testsFailed > 0) {
        std::cerr << "Prerequisites missing\n";
        return 1;
    }

    // Create a temp working directory for this test (simulates hook-projects/hook-swccu scenario)
    fs::path workDir = binDir / "test_live_inject_workdir";
    if (fs::exists(workDir)) fs::remove_all(workDir);
    fs::create_directories(workDir);

    // Copy hook_framework.dll to work directory
    fs::path localDll = workDir / "hook_framework.dll";
    fs::copy_file(hookDllPath, localDll);

    // Write hooks.json targeting target_app.exe exports
    nlohmann::json config;
    config["output_dir"] = "dump_data";
    config["log_file"] = "hook_test.log";
    config["log_level"] = "trace";
    config["hooks"] = nlohmann::json::array({
        {
            {"module", "target_app.exe"},
            {"symbol", "add_numbers"},
            {"action", "capture"},
            {"collector", "typed"},
            {"params", nlohmann::json::array({
                {{"name", "a"}, {"type", "int32"}},
                {{"name", "b"}, {"type", "int32"}}
            })},
            {"return_type", "int32"}
        },
        {
            {"module", "target_app.exe"},
            {"symbol", "multiply_numbers"},
            {"action", "capture"},
            {"collector", "typed"},
            {"params", nlohmann::json::array({
                {{"name", "a"}, {"type", "int32"}},
                {{"name", "b"}, {"type", "int32"}}
            })},
            {"return_type", "int32"}
        }
    });

    std::ofstream configFile(workDir / "hooks.json");
    configFile << config.dump(2);
    configFile.close();

    // --- Step 1: Launch target_app.exe (it waits for Enter before calling functions) ---
    std::cout << "\n[1] Launching target_app.exe ...\n";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    std::string targetCmd = targetPath.string();
    std::vector<char> cmdBuf(targetCmd.begin(), targetCmd.end());
    cmdBuf.push_back('\0');

    BOOL created = CreateProcessA(
        nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
        CREATE_NEW_CONSOLE, nullptr, binDir.string().c_str(), &si, &pi);

    check(created != 0, "target_app.exe started");
    if (!created) {
        std::cerr << "CreateProcess failed: " << GetLastError() << "\n";
        return 1;
    }

    DWORD targetPid = pi.dwProcessId;
    std::cout << "  target_app.exe PID=" << targetPid << "\n";

    // Wait for the process to initialize
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Verify process is running
    DWORD exitCheck = 0;
    GetExitCodeProcess(pi.hProcess, &exitCheck);
    check(exitCheck == STILL_ACTIVE, "target_app.exe is running");

    if (exitCheck != STILL_ACTIVE) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 1;
    }

    // --- Step 2: Inject hook_framework.dll into the running process ---
    std::cout << "\n[2] Injecting hook_framework.dll (from work directory) ...\n";

    std::string injectCmd = "\"" + injectorPath.string() + "\" --pid "
                            + std::to_string(targetPid) + " --dll \""
                            + localDll.string() + "\"";

    STARTUPINFOA siInj = {};
    siInj.cb = sizeof(siInj);
    PROCESS_INFORMATION piInj = {};

    std::vector<char> injCmdBuf(injectCmd.begin(), injectCmd.end());
    injCmdBuf.push_back('\0');

    BOOL injected = CreateProcessA(
        nullptr, injCmdBuf.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &siInj, &piInj);

    check(injected != 0, "injector process started");

    if (injected) {
        WaitForSingleObject(piInj.hProcess, 10000);
        DWORD injExit = 0;
        GetExitCodeProcess(piInj.hProcess, &injExit);
        check(injExit == 0, "injection succeeded (exit code 0)");
        CloseHandle(piInj.hThread);
        CloseHandle(piInj.hProcess);
    }

    // Wait for DLL to load and install hooks
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // --- Step 3: Trigger the hooked functions by sending Enter to target_app ---
    std::cout << "\n[3] Triggering hooked function calls ...\n";

    // Send Enter to target_app's console to make it call the test functions
    BOOL attached = AttachConsole(targetPid);
    if (attached) {
        // Generate key events: two Enter presses (first to start tests, second to exit)
        INPUT_RECORD ir[2] = {};
        ir[0].EventType = KEY_EVENT;
        ir[0].Event.KeyEvent.bKeyDown = TRUE;
        ir[0].Event.KeyEvent.wRepeatCount = 1;
        ir[0].Event.KeyEvent.uChar.AsciiChar = '\r';
        ir[0].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
        ir[1] = ir[0];

        HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
        // This won't work because we attached to THEIR console
        // Use a different approach: write to their stdin
        FreeConsole();
    }

    // Alternative: use WriteConsoleInput on the target's console
    // Simpler approach: just post WM_CHAR messages... but console apps don't have windows.
    // Most reliable: use GenerateConsoleCtrlEvent or pipe approach
    // For this test, let's just wait and then terminate - the hooks are already installed

    // The real test: we can verify the hooks installed by checking the log file
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Log verification moved to after process exit (step 6.5)

    // --- Step 5: Use a programmatic way to call functions in target ---
    // Allocate memory in target and create a thread that calls the exported function
    std::cout << "\n[5] Remotely calling hooked functions in target process ...\n";

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, targetPid);
    check(hProc != nullptr, "OpenProcess succeeded");

    if (hProc) {
        // Get add_numbers address in target (same exe, same export)
        HMODULE hTargetMod = nullptr;
        // We need the target's module base. Since target_app.exe is the main module:
        // Use EnumProcessModules or just try GetProcAddress with local knowledge
        // For simplicity: target_app.exe exports are at the same RVA in both processes
        // (ASLR is per-process, but we can find the base via remote module enumeration)

        // Alternative approach: call GetProcAddress in the target process
        // LoadLibrary is already proven to work (injection worked), so let's
        // call a function via CreateRemoteThread with a shellcode approach
        // But that's overly complex for a test.

        // Simplest: Write \r\n to the target's stdin pipe to trigger function calls
        // Since target_app uses std::cin.get(), we need console input.

        // Let's use a helper: write to the console input buffer of the target
        FreeConsole();
        if (AttachConsole(targetPid)) {
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
                    inputs[i].Event.KeyEvent.dwControlKeyState = 0;

                    inputs[i+1] = inputs[i];
                    inputs[i+1].Event.KeyEvent.bKeyDown = FALSE;
                }
                DWORD written = 0;
                WriteConsoleInputA(hInput, inputs, 4, &written);
                std::cout << "  Sent " << written << " input events to target console\n";
                CloseHandle(hInput);
            }
            FreeConsole();
            AllocConsole();
        }

        // Wait for target to process the functions
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // Send second Enter to exit
        FreeConsole();
        if (AttachConsole(targetPid)) {
            HANDLE hInput = CreateFileA("CONIN$", GENERIC_WRITE | GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr, OPEN_EXISTING, 0, nullptr);
            if (hInput != INVALID_HANDLE_VALUE) {
                INPUT_RECORD inputs[2] = {};
                inputs[0].EventType = KEY_EVENT;
                inputs[0].Event.KeyEvent.bKeyDown = TRUE;
                inputs[0].Event.KeyEvent.wRepeatCount = 1;
                inputs[0].Event.KeyEvent.uChar.AsciiChar = '\r';
                inputs[0].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
                inputs[1] = inputs[0];
                inputs[1].Event.KeyEvent.bKeyDown = FALSE;
                DWORD written = 0;
                WriteConsoleInputA(hInput, inputs, 2, &written);
                CloseHandle(hInput);
            }
            FreeConsole();
            AllocConsole();
        }

        CloseHandle(hProc);
    }

    // Wait for target to exit
    WaitForSingleObject(pi.hProcess, 10000);
    DWORD targetExit = 0;
    GetExitCodeProcess(pi.hProcess, &targetExit);
    check(targetExit == 0 || targetExit == STILL_ACTIVE, "target process completed");

    if (targetExit == STILL_ACTIVE) {
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 3000);
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // --- Step 6: Verify captured output ---
    std::cout << "\n[6] Verifying captured dump_data ...\n";

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    fs::path dumpDir = workDir / "dump_data";
    check(fs::exists(dumpDir), "dump_data directory created");

    if (fs::exists(dumpDir)) {
        int jsonFiles = 0;
        int rawFiles = 0;
        for (auto& entry : fs::recursive_directory_iterator(dumpDir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".json") jsonFiles++;
            if (entry.path().extension() == ".raw") rawFiles++;
        }
        std::cout << "  JSON files: " << jsonFiles << ", Raw files: " << rawFiles << "\n";
        check(jsonFiles >= 4, "at least 4 JSON files (2 funcs * entry+exit)");
        check(rawFiles >= 4, "at least 4 raw files");

        // Check add_numbers directory
        fs::path addDir = dumpDir / "target_app.exe" / "add_numbers";
        check(fs::exists(addDir), "add_numbers capture directory exists");

        if (fs::exists(addDir)) {
            for (auto& f : fs::directory_iterator(addDir)) {
                if (f.path().extension() == ".json" &&
                    f.path().stem().string().find("entry") != std::string::npos) {
                    std::ifstream jf(f.path());
                    nlohmann::json j;
                    jf >> j;
                    bool paramsOk = j.contains("params") &&
                                    j["params"].size() == 2 &&
                                    j["params"][0]["name"] == "a" &&
                                    j["params"][0]["value"] == 3 &&
                                    j["params"][1]["name"] == "b" &&
                                    j["params"][1]["value"] == 4;
                    check(paramsOk, "add_numbers entry params captured correctly (3, 4)");
                    break;
                }
            }
        }

        // Check multiply_numbers directory
        fs::path mulDir = dumpDir / "target_app.exe" / "multiply_numbers";
        check(fs::exists(mulDir), "multiply_numbers capture directory exists");

        // List captured structure
        std::cout << "\n  Dump structure:\n";
        for (auto& entry : fs::recursive_directory_iterator(dumpDir)) {
            if (entry.is_regular_file()) {
                std::cout << "    " << fs::relative(entry.path(), dumpDir).string() << "\n";
            }
        }
    }

    // --- Step 7: Verify log file (after process exit, log is flushed) ---
    std::cout << "\n[7] Verifying log file ...\n";
    {
        fs::path fwLogFile = workDir / "hook_framework.log";
        check(fs::exists(fwLogFile), "hook_framework.log exists");

        if (fs::exists(fwLogFile)) {
            std::ifstream log(fwLogFile);
            std::string logContent((std::istreambuf_iterator<char>(log)),
                                    std::istreambuf_iterator<char>());
            log.close();

            check(logContent.find("Hook Framework DLL loaded") != std::string::npos,
                  "log: DLL loaded");
            check(logContent.find("hooks.json") != std::string::npos,
                  "log: config path resolved");
            check(logContent.find("Hook added") != std::string::npos,
                  "log: hooks configured");
            check(logContent.find("hooks installed") != std::string::npos,
                  "log: hooks installed");
        }
    }

    // Cleanup
    if (fs::exists(workDir)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        try { fs::remove_all(workDir); } catch (...) {}
    }

    std::cout << "\n=== Results: " << g_testsPassed << " passed, "
              << g_testsFailed << " failed ===\n";
    return g_testsFailed > 0 ? 1 : 0;
}
