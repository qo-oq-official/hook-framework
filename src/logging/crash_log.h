#pragma once
#include <Windows.h>
#include <string>
#include <cstring>

namespace hookfw {
namespace crash_log {

inline HANDLE g_handle = INVALID_HANDLE_VALUE;

inline void init(const char* logName, bool append = true) {
    return;
    if (g_handle != INVALID_HANDLE_VALUE) return;
    g_handle = CreateFileA(logName, GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, append ? OPEN_ALWAYS : CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_handle != INVALID_HANDLE_VALUE) {
        SetFilePointer(g_handle, 0, nullptr, FILE_END);
    }
}

inline void reinit(const char* logName, bool append = true) {
    return;
    if (g_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_handle);
    }
    g_handle = CreateFileA(logName, GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, append ? OPEN_ALWAYS : CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_handle != INVALID_HANDLE_VALUE) {
        SetFilePointer(g_handle, 0, nullptr, FILE_END);
    }
}

inline void write(const char* msg) {
    return;
    if (g_handle == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(g_handle, msg, static_cast<DWORD>(strlen(msg)), &written, nullptr);
    FlushFileBuffers(g_handle);
}

inline void write(const wchar_t* msg) {
    return;
    if (g_handle == INVALID_HANDLE_VALUE) return;
    DWORD bytesToWrite = static_cast<DWORD>(wcslen(msg) * sizeof(wchar_t));
    DWORD bytesWritten = 0;
    WriteFile(g_handle, msg, bytesToWrite, &bytesWritten, nullptr);
    FlushFileBuffers(g_handle);
}

}
}
