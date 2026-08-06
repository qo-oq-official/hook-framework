#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <string>
#include <filesystem>

DWORD findProcessByName(const std::string& name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        std::cerr << "ERROR: CreateToolhelp32Snapshot failed\n";
        return 0;
    }

    PROCESSENTRY32 entry = {};
    entry.dwSize = sizeof(entry);

    if (Process32First(snap, &entry)) {
        do {
            if (_stricmp(entry.szExeFile, name.c_str()) == 0) {
                CloseHandle(snap);
                return entry.th32ProcessID;
            }
        } while (Process32Next(snap, &entry));
    }

    CloseHandle(snap);
    return 0;
}

bool injectDll(DWORD pid, const std::string& dllPath) {
    std::string fullPath = std::filesystem::absolute(dllPath).string();
    std::cout << "Injecting: " << fullPath << " -> PID " << pid << "\n";

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        std::cerr << "ERROR: OpenProcess failed, pid=" << pid
                  << ", error=" << GetLastError() << "\n";
        return false;
    }

    size_t pathSize = fullPath.size() + 1;
    void* remoteMem = VirtualAllocEx(hProcess, nullptr, pathSize,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) {
        std::cerr << "ERROR: VirtualAllocEx failed, error=" << GetLastError() << "\n";
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, remoteMem, fullPath.c_str(), pathSize, nullptr)) {
        std::cerr << "ERROR: WriteProcessMemory failed, error=" << GetLastError() << "\n";
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    auto pLoadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(hKernel32, "LoadLibraryA"));

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, pLoadLibrary, remoteMem, 0, nullptr);
    if (!hThread) {
        std::cerr << "ERROR: CreateRemoteThread failed, error=" << GetLastError() << "\n";
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    std::cout << "Injection thread created, waiting...\n";
    WaitForSingleObject(hThread, 10000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    if (exitCode == 0) {
        std::cerr << "WARNING: LoadLibrary returned NULL in target process\n";
        return false;
    }

    std::cout << "DLL injected successfully! Module handle = 0x"
              << std::hex << exitCode << std::dec << "\n";
    return true;
}

int main(int argc, char* argv[]) {
    std::cout << "=== Hook Framework Injector ===\n";

    if (argc < 3 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        std::cout << R"(DESCRIPTION:
  Injects a DLL into a target process for API hooking.

MODES:
  --pid <PID>       Inject into running process by PID
  --name <NAME>     Inject into running process by name
  --exe <PATH>      Create suspended process, inject DLL, resume and wait

OPTIONS:
  --dll <PATH>      Path to DLL to inject (required)
  --args <ARGS>     Arguments passed to target exe (--exe mode only)
  --help, -h        Show this help

EXAMPLES:
  injector --pid 1234 --dll hook_framework.dll
  injector --name notepad.exe --dll hook_framework.dll
  injector --exe target.exe --dll hook_framework.dll
  injector --exe target.exe --dll hook_framework.dll --args "the target args"
)";
        return argc < 3 ? 1 : 0;
    }

    DWORD pid = 0;
    std::string dllPath;
    std::string exePath;
    std::string processName;
    std::string exeArgs;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--pid" && i + 1 < argc) pid = std::stoul(argv[++i]);
        else if (arg == "--name" && i + 1 < argc) processName = argv[++i];
        else if (arg == "--dll" && i + 1 < argc) dllPath = argv[++i];
        else if (arg == "--exe" && i + 1 < argc) exePath = argv[++i];
        else if (arg == "--args" && i + 1 < argc) exeArgs = argv[++i];
    }

    if (dllPath.empty()) {
        std::cerr << "ERROR: --dll is required\n";
        return 1;
    }

    if (!std::filesystem::exists(dllPath)) {
        std::cerr << "ERROR: DLL not found: " << dllPath << "\n";
        return 1;
    }

    if (!exePath.empty()) {
        std::string cmdLine = "\"" + exePath + "\"";
        if (!exeArgs.empty()) cmdLine += " " + exeArgs;

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};

        std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back('\0');

        if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                            CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
            std::cerr << "ERROR: CreateProcess failed, error=" << GetLastError() << "\n";
            return 1;
        }

        std::cout << "Started process PID=" << pi.dwProcessId << " (suspended)\n";
        pid = pi.dwProcessId;

        if (!injectDll(pid, dllPath)) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return 1;
        }

        ResumeThread(pi.hThread);
        std::cout << "Process resumed\n";

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD procExitCode = 0;
        GetExitCodeProcess(pi.hProcess, &procExitCode);
        std::cout << "Process exited with code " << procExitCode << "\n";

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        if (pid == 0 && !processName.empty()) {
            pid = findProcessByName(processName);
            if (pid == 0) {
                std::cerr << "ERROR: Process not found: " << processName << "\n";
                return 1;
            }
            std::cout << "Found process '" << processName << "' PID=" << pid << "\n";
        }

        if (pid == 0) {
            std::cerr << "ERROR: No target specified (use --pid, --name, or --exe)\n";
            return 1;
        }

        if (!injectDll(pid, dllPath)) {
            return 1;
        }
    }

    return 0;
}
