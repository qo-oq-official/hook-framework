#pragma once
#include "replacer/replacer_base.h"
#include <Windows.h>

namespace hookfw {

class DllReplacer : public ReplacerBase {
public:
    DllReplacer(const std::string& dllPath, const std::string& funcName);
    ~DllReplacer() override;

    void* getReplacementAddress() override;
    std::string description() const override;

private:
    std::string dllPath_;
    std::string funcName_;
    HMODULE hModule_ = nullptr;
    void* funcAddr_ = nullptr;
};

}
