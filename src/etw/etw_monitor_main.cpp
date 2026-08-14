#include "etw/etw_monitor.h"

#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <string>

int main() {
    _setmode(_fileno(stdout), _O_U16TEXT);

    hookfw::EtwImageLoadMonitor monitor;

    ULONG status = monitor.start([](ULONG processId, ULONG_PTR imageBase,
                                    const std::wstring& filePath) {
        if (processId != GetCurrentProcessId()) {
            return;
        }
        wprintf(L"[+] DLL Loaded | pid: %lu | Base: 0x%p | Path: %s\n",
                processId, reinterpret_cast<void*>(imageBase), filePath.c_str());
    });

    if (status != ERROR_SUCCESS) {
        wprintf(L"Failed to start ETW monitoring, error=%lu "
                L"(run as Administrator / Performance Log Users)\n", status);
        return 1;
    }

    wprintf(L"ETW Image Load monitoring active. Loading a DLL to test...\n");

    HMODULE hMod = LoadLibraryW(L"test_pdb_target.dll");
    Sleep(5000);

    if (hMod) {
        FreeLibrary(hMod);
    }

    monitor.stop();
    wprintf(L"Monitoring stopped.\n");
    return 0;
}
