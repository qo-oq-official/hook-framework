#include <Windows.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
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
    std::cout << "=== Hook Framework Injection Test ===\n\n";

    fs::path binDir = fs::current_path();
    fs::path injectorPath = binDir / "injector.exe";
    fs::path targetPath = binDir / "target_app.exe";
    fs::path dllPath = binDir / "hook_framework.dll";
    fs::path configPath = binDir / "hooks.json";
    fs::path dumpDir = binDir / "dump_data";

    check(fs::exists(injectorPath), "injector.exe exists");
    check(fs::exists(targetPath), "target_app.exe exists");
    check(fs::exists(dllPath), "hook_framework.dll exists");
    check(fs::exists(configPath), "hooks.json exists");

    if (g_testsFailed > 0) {
        std::cerr << "Prerequisites missing, cannot continue\n";
        return 1;
    }

    if (fs::exists(dumpDir)) {
        fs::remove_all(dumpDir);
    }

    std::cout << "\n[1] Launching injection test...\n";

    std::string cmd = "cmd /c \"echo. | echo. | \""
                      + injectorPath.string()
                      + "\" --exe \"" + targetPath.string()
                      + "\" --dll \"" + dllPath.string() + "\"\"";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    BOOL ok = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, binDir.string().c_str(), &si, &pi);
    check(ok != 0, "CreateProcess for injector");

    if (ok) {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 30000);
        check(waitResult == WAIT_OBJECT_0, "process completed within 30s");

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        check(exitCode == 0, "target_app exited with code 0");

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    std::cout << "\n[2] Verifying dump_data output...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    check(fs::exists(dumpDir), "dump_data directory created");

    if (fs::exists(dumpDir)) {
        int jsonFiles = 0;
        int rawFiles = 0;
        for (auto& entry : fs::recursive_directory_iterator(dumpDir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".json") jsonFiles++;
            if (entry.path().extension() == ".raw") rawFiles++;
        }
        check(jsonFiles >= 6, "at least 6 JSON files captured");
        check(rawFiles >= 6, "at least 6 raw files captured");

        auto addDir = dumpDir / "target_app.exe" / "add_numbers";
        check(fs::exists(addDir), "add_numbers directory exists");
    }

    check(fs::exists(binDir / "hook_framework.log"), "log file created");

    std::cout << "\n=== Results: " << g_testsPassed << " passed, "
              << g_testsFailed << " failed ===\n";

    return g_testsFailed > 0 ? 1 : 0;
}
