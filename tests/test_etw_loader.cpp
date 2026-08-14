#include <Windows.h>
#include <cstdio>

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    printf("[DllMain] entered\n");
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        printf("[DllMain] ATTACH\n");
        break;
    case DLL_PROCESS_DETACH:
        printf("[DllMain] DETACH\n");
        break;
    }
    return TRUE;
}