#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "perfetto.h"

struct RegisterContext;
namespace hookfw {
struct StackInfo {
    uint64_t rva;
    std::string mod;

    void WriteIntoTracedValue(perfetto::TracedValue ctx) const;
};
StackInfo CaptureCallStackAddresses(const RegisterContext* ctx);
}