#pragma once

#include <windows.h>

#include <sstream>
#include <string>
#include <vector>

namespace hookfw {

// 将单个参数用双引号包裹并转义内部引号，用于拼接命令行
inline std::string QuoteArg(const std::string& arg) {
    std::string q = "\"";
    for (char c : arg) {
        if (c == '"') {
            q += "\\\"";
        } else {
            q += c;
        }
    }
    q += "\"";
    return q;
}

// 执行外部进程并捕获其 stdout，返回退出码；进程创建失败返回 -1。
inline int RunProcessCapture(const std::string& exe,
                             const std::vector<std::string>& args,
                             std::string& out) {
    out.clear();

    std::string cmd = QuoteArg(exe);
    for (const auto& a : args) {
        cmd += " " + QuoteArg(a);
    }

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        return -1;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);  // stderr 直通父进程，避免混入 stdout
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {};
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, 0,
                        nullptr, nullptr, &si, &pi)) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return -1;
    }
    CloseHandle(writePipe);

    std::ostringstream oss;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(readPipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
        oss.write(buf, static_cast<std::streamsize>(n));
    }
    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    out = oss.str();
    return static_cast<int>(code);
}

}  // namespace hookfw
