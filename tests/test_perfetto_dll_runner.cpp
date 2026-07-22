#include <cstdio>
#include <Windows.h>

int main() {
    printf("=== Perfetto DllMain Init + Multi-Threaded Test ===\n");
    printf("Loading test_perfetto_dll.dll (init in DLL_PROCESS_ATTACH)...\n");

    HMODULE hDll = LoadLibraryA("test_perfetto_dll.dll");
    if (!hDll) {
        printf("[FAIL] LoadLibrary failed (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("[PASS] DLL loaded — DllMain ATTACH returned\n");

    auto mtFn = reinterpret_cast<void(*)()>(
        GetProcAddress(hDll, "WriteMultiThreaded"));
    if (mtFn) {
        printf("\nCalling WriteMultiThreaded()...\n");
        mtFn();
    } else {
        printf("[WARN] WriteMultiThreaded not found\n");
    }

    auto shutdownFn = reinterpret_cast<void(*)()>(
        GetProcAddress(hDll, "ShutdownAndSave"));
    if (!shutdownFn) {
        printf("[FAIL] ShutdownAndSave not found\n");
        return 2;
    }

    printf("\nCalling ShutdownAndSave()...\n");
    shutdownFn();
    printf("[PASS] ShutdownAndSave returned\n");

    printf("\nUnloading DLL...\n");
    FreeLibrary(hDll);
    printf("[PASS] FreeLibrary returned — no crash\n");

    return 0;
}
