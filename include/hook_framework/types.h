#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace hookfw {

enum class ParamType : uint8_t {
    Int8,
    Int16,
    Int32,
    Int64,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Float32,
    Float64,
    Pointer,
    CString,
    WString,
    WStringRef,
    Buffer,
};

enum class HookAction : uint8_t {
    Capture,
    Replace,
    CaptureAndReplace,
};

enum class CollectorType : uint8_t {
    Raw,
    Typed,
    Custom,
};

struct ParamDescriptor {
    std::string name;
    ParamType type;
    uint32_t bufferSize = 0;
    ParamType derefType = ParamType::Pointer;
    bool hasDeref = false;
};

struct HookTarget {
    std::string moduleName;
    std::string symbolName;
    uint64_t rva = 0;
    bool useRva = false;
};

struct HookDescriptor {
    HookTarget target;
    HookAction action = HookAction::Capture;
    CollectorType collectorType = CollectorType::Raw;
    std::vector<ParamDescriptor> params;
    ParamType returnType = ParamType::Int64;
    std::string replacerModule;
    std::string replacerSymbol;
    std::string customCollectorName;
};

}
