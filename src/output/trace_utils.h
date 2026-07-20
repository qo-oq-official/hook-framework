#pragma once
#include <string>
#include <Windows.h>
#include <DbgHelp.h>

#pragma comment(lib, "dbghelp.lib")

namespace hookfw {

inline std::string undecorateName(const std::string& mangledName) {
    if (mangledName.empty() || mangledName[0] != '?') {
        return mangledName;
    }
    char buf[1024] = {};
    DWORD flags = UNDNAME_NO_ACCESS_SPECIFIERS |
                  UNDNAME_NO_MEMBER_TYPE |
                  UNDNAME_NO_MS_KEYWORDS |
                  UNDNAME_NO_RETURN_UDT_MODEL |
                  UNDNAME_NO_THISTYPE |
                  UNDNAME_NO_ALLOCATION_MODEL;
    DWORD result = UnDecorateSymbolName(mangledName.c_str(), buf, sizeof(buf), flags);
    if (result > 0) {
        return std::string(buf);
    }
    return mangledName;
}

}
