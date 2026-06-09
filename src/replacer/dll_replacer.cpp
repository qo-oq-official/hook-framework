#include "replacer/dll_replacer.h"
#include "logging/logger.h"

namespace hookfw {

DllReplacer::DllReplacer(const std::string& dllPath, const std::string& funcName)
    : dllPath_(dllPath), funcName_(funcName) {

    hModule_ = LoadLibraryA(dllPath.c_str());
    if (!hModule_) {
        LOG_ERROR("DllReplacer: Failed to load DLL '{}', error={}", dllPath, GetLastError());
        return;
    }

    funcAddr_ = reinterpret_cast<void*>(GetProcAddress(hModule_, funcName.c_str()));
    if (!funcAddr_) {
        LOG_ERROR("DllReplacer: Failed to find '{}' in '{}', error={}",
                  funcName, dllPath, GetLastError());
        return;
    }

    LOG_INFO("DllReplacer: loaded {}!{} at {:#x}", dllPath, funcName,
             reinterpret_cast<uintptr_t>(funcAddr_));
}

DllReplacer::~DllReplacer() {
    if (hModule_) {
        FreeLibrary(hModule_);
        LOG_TRACE("DllReplacer: unloaded {}", dllPath_);
    }
}

void* DllReplacer::getReplacementAddress() {
    return funcAddr_;
}

std::string DllReplacer::description() const {
    return dllPath_ + "!" + funcName_;
}

}
