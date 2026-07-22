#include <cstdio>
#include <Windows.h>

int main() {
    printf("=== Perfetto DLL Test (matches framework architecture) ===\n");
    printf("Init:   StartTracing export (after DllMain returns)\n");
    printf("Events: WriteMultiThreaded export (4 threads x 100 BEGIN/END pairs)\n");
    printf("Shutdown: ShutdownAndSave export (before FreeLibrary)\n\n");

    HMODULE hDll = LoadLibraryA("test_perfetto_dll.dll");
    if (!hDll) {
        printf("[FAIL] LoadLibrary failed (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("[PASS] DLL loaded (DllMain ATTACH: no Perfetto code)\n");

    auto startFn = reinterpret_cast<void(*)()>(
        GetProcAddress(hDll, "StartTracing"));
    if (!startFn) {
        printf("[FAIL] StartTracing not found\n");
        return 2;
    }
    printf("\nCalling StartTracing()...\n");
    startFn();
    printf("[PASS] StartTracing returned\n");

    auto mtFn = reinterpret_cast<void(*)()>(
        GetProcAddress(hDll, "WriteMultiThreaded"));
    if (mtFn) {
        printf("\nCalling WriteMultiThreaded()...\n");
        mtFn();
        printf("[PASS] WriteMultiThreaded returned\n");
    }

    auto shutdownFn = reinterpret_cast<void(*)()>(
        GetProcAddress(hDll, "ShutdownAndSave"));
    if (!shutdownFn) {
        printf("[FAIL] ShutdownAndSave not found\n");
        return 3;
    }
    printf("\nCalling ShutdownAndSave() (before FreeLibrary)...\n");
    shutdownFn();
    printf("[PASS] ShutdownAndSave returned\n");

    printf("\nUnloading DLL...\n");
    FreeLibrary(hDll);
    printf("[PASS] FreeLibrary returned — clean exit\n");

    return 0;
}
