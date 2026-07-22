#include <cstdio>
#include <Windows.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

static bool tcpSend(const char* cmd) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int r = connect(s, (sockaddr*)&addr, sizeof(addr));
    if (r != 0) { printf("[FAIL] connect failed (err=%d)\n", WSAGetLastError()); closesocket(s); WSACleanup(); return false; }

    send(s, cmd, (int)strlen(cmd), 0);
    shutdown(s, SD_SEND);

    char buf[16] = {};
    recv(s, buf, sizeof(buf) - 1, 0);
    printf("       response: %s", buf);
    closesocket(s);
    WSACleanup();
    return strncmp(buf, "ok", 2) == 0;
}

int main() {
    printf("=== Perfetto TCP Control Test ===\n");
    printf("Commands: start, multi, stop via TCP port 12345\n\n");

    HMODULE hDll = LoadLibraryA("test_perfetto_dll.dll");
    if (!hDll) {
        printf("[FAIL] LoadLibrary failed (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("[PASS] DLL loaded, TCP listener started\n");

    Sleep(500);

    printf("\n--- Sending 'start' ---\n");
    if (!tcpSend("start")) { printf("[FAIL] start command\n"); return 2; }

    Sleep(500);

    printf("\n--- Sending 'multi' ---\n");
    if (!tcpSend("multi")) { printf("[FAIL] multi command\n"); return 3; }

    Sleep(500);

    printf("\n--- Sending 'stop' ---\n");
    if (!tcpSend("stop")) { printf("[FAIL] stop command\n"); return 4; }

    Sleep(500);

    printf("\nUnloading DLL...\n");
    FreeLibrary(hDll);
    printf("[PASS] FreeLibrary returned — clean exit\n");

    return 0;
}
