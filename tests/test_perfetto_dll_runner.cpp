#include <cstdio>
#include <Windows.h>

int main() {
    printf("=== Perfetto DLL Main Test (shutdown BEFORE FreeLibrary) ===\n");
    printf("Loading test_perfetto_dll.dll...\n");

    HMODULE hDll = LoadLibraryA("test_perfetto_dll.dll");
    if (!hDll) {
        printf("[FAIL] LoadLibrary failed (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("[PASS] DLL loaded\n");

    auto startFn = reinterpret_cast<void(*)()>(
        GetProcAddress(hDll, "StartTracing"));
    if (!startFn) {
        printf("[FAIL] StartTracing not found\n");
        return 2;
    }

    printf("Calling StartTracing()...\n");
    startFn();

    auto shutdownFn = reinterpret_cast<void(*)()>(
        GetProcAddress(hDll, "ShutdownAndSave"));
    if (!shutdownFn) {
        printf("[FAIL] ShutdownAndSave not found\n");
        return 3;
    }

    printf("\nCalling ShutdownAndSave() (BEFORE FreeLibrary)...\n");
    shutdownFn();
    printf("[PASS] ShutdownAndSave returned\n");

    printf("\nUnloading DLL...\n");
    FreeLibrary(hDll);
    printf("[PASS] FreeLibrary returned — no crash\n");

    return 0;
}
