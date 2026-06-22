#pragma once
#include "thunk/hook_context.h"
#include "hook_framework/types.h"
#include <nlohmann/json.hpp>
#include <format>
#include <vector>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <Windows.h>

namespace hookfw {

class TypedCollector;

inline std::string wideToUtf8(const wchar_t* wstr, size_t len) {
    if (len == 0) return "";
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return "";
    std::string result(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(len), &result[0], utf8Len, nullptr, nullptr);
    return result;
}

inline bool isLikelyWideString(const wchar_t* ptr, size_t* outLen) {
    if (!ptr) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(ptr, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    size_t maxLen = (reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize - reinterpret_cast<uintptr_t>(ptr)) / 2;
    if (maxLen > 4096) maxLen = 4096;
    for (size_t i = 0; i < maxLen; i++) {
        if (ptr[i] == 0) { *outLen = i; return i > 0; }
        if (static_cast<uint16_t>(ptr[i]) > 0 && static_cast<uint16_t>(ptr[i]) < 0x20 && ptr[i] != L'\r' && ptr[i] != L'\n' && ptr[i] != L'\t')
            return false;
    }
    *outLen = maxLen;
    return maxLen > 0;
}

inline nlohmann::json extractTypedParam(const HookContext& ctx, int index,
                                        const ParamDescriptor& desc) {
    nlohmann::json param;
    param["name"] = desc.name;
    param["index"] = index;

    switch (desc.type) {
        case ParamType::Int8:
            param["type"] = "int8";
            param["value"] = static_cast<int8_t>(ctx.arg<int64_t>(index));
            break;
        case ParamType::Int16:
            param["type"] = "int16";
            param["value"] = static_cast<int16_t>(ctx.arg<int64_t>(index));
            break;
        case ParamType::Int32:
            param["type"] = "int32";
            param["value"] = ctx.arg<int32_t>(index);
            break;
        case ParamType::Int64:
            param["type"] = "int64";
            param["value"] = ctx.arg<int64_t>(index);
            break;
        case ParamType::UInt8:
            param["type"] = "uint8";
            param["value"] = static_cast<uint8_t>(ctx.arg<uint64_t>(index));
            break;
        case ParamType::UInt16:
            param["type"] = "uint16";
            param["value"] = static_cast<uint16_t>(ctx.arg<uint64_t>(index));
            break;
        case ParamType::UInt32:
            param["type"] = "uint32";
            param["value"] = ctx.arg<uint32_t>(index);
            break;
        case ParamType::UInt64:
            param["type"] = "uint64";
            param["value"] = ctx.arg<uint64_t>(index);
            break;
        case ParamType::Float32:
            param["type"] = "float32";
            param["value"] = ctx.argFloat(index);
            break;
        case ParamType::Float64:
            param["type"] = "float64";
            param["value"] = ctx.argDouble(index);
            break;
        case ParamType::Pointer:
            param["type"] = "pointer";
            param["value"] = std::format("{:#x}", ctx.arg<uint64_t>(index));
            break;
        case ParamType::CString:
        case ParamType::CStringRef: {
            param["type"] = desc.type == ParamType::CStringRef ? "cstring_ref" : "cstring";
            auto ptr = ctx.arg<const char*>(index);
            try {
                if (ptr && !IsBadReadPtr(ptr, 1))
                    param["value"] = std::string(ptr, strnlen(ptr, 1024));
                else
                    param["value"] = nullptr;
            } catch (...) { param["value"] = "<unreadable>"; }
            break;
        }
        case ParamType::WString:
        case ParamType::WStringRef: {
            param["type"] = desc.type == ParamType::WStringRef ? "wstring_ref" : "wstring";
            auto ptr = ctx.arg<const wchar_t*>(index);
            try {
                size_t len = 0;
                if (ptr && isLikelyWideString(ptr, &len) && len > 0) {
                    std::string utf8 = wideToUtf8(ptr, len);
                    param["value"] = utf8.empty()
                        ? std::format("<non-text@{:#x}>", reinterpret_cast<uintptr_t>(ptr)) : utf8;
                } else if (ptr && !IsBadReadPtr(ptr, 1)) {
                    auto cp = reinterpret_cast<const char*>(ptr);
                    size_t sl = strnlen(cp, 256);
                    if (sl > 0) { param["value"] = std::string(cp, sl); param["type"] = "string_fallback"; }
                    else param["value"] = std::format("<non-text@{:#x}>", reinterpret_cast<uintptr_t>(ptr));
                } else param["value"] = nullptr;
            } catch (...) { param["value"] = "<unreadable>"; }
            break;
        }
        case ParamType::Buffer: {
            param["type"] = "buffer";
            auto ptr = ctx.arg<const char*>(index);
            uint32_t sz = desc.bufferSize > 0 ? desc.bufferSize : ctx.arg<uint32_t>(index + 1);
            if (ptr && sz > 0 && sz <= 4096 && !IsBadReadPtr(ptr, sz)) {
                param["value"] = std::string(ptr, ptr + (std::min)(sz, 256u));
            } else {
                param["value"] = std::format("{:#x}", reinterpret_cast<uintptr_t>(ptr));
            }
            break;
        }
        default:
            param["type"] = "int64";
            param["value"] = ctx.arg<int64_t>(index);
            break;
    }
    return param;
}

struct ParamSerializer {
    static void addDefaultEntry(nlohmann::json& args, const HookContext& ctx) {
        args["arg0"] = std::format("{:#x}", ctx.arg<uint64_t>(0));
        args["arg1"] = std::format("{:#x}", ctx.arg<uint64_t>(1));
        args["arg2"] = std::format("{:#x}", ctx.arg<uint64_t>(2));
        args["arg3"] = std::format("{:#x}", ctx.arg<uint64_t>(3));
    }

    static void addDefaultExit(nlohmann::json& args, const HookContext& ctx) {
        args["return"] = std::format("{:#x}", ctx.returnValue<uint64_t>());
    }

    static void addConfigEntry(nlohmann::json& args, const HookContext& ctx,
                               const std::vector<ParamDescriptor>& params) {
        for (size_t i = 0; i < params.size(); i++) {
            auto p = extractTypedParam(ctx, static_cast<int>(i), params[i]);
            std::string key = p.value("name",
                std::string("arg") + std::to_string(i));
            if (p.contains("value"))
                args[key] = p["value"];
            else if (p.contains("value_hex"))
                args[key] = p["value_hex"];
        }
    }

    static void addConfigExit(nlohmann::json& args, const HookContext& ctx,
                              const std::vector<ParamDescriptor>& params,
                              ParamType returnType) {
        switch (returnType) {
            case ParamType::Int8:    args["return"] = static_cast<int8_t>(ctx.returnValue<int64_t>()); break;
            case ParamType::Int16:   args["return"] = static_cast<int16_t>(ctx.returnValue<int64_t>()); break;
            case ParamType::Int32:   args["return"] = ctx.returnValue<int32_t>(); break;
            case ParamType::Int64:   args["return"] = ctx.returnValue<int64_t>(); break;
            case ParamType::UInt8:   args["return"] = static_cast<uint8_t>(ctx.returnValue<uint64_t>()); break;
            case ParamType::UInt16:  args["return"] = static_cast<uint16_t>(ctx.returnValue<uint64_t>()); break;
            case ParamType::UInt32:  args["return"] = ctx.returnValue<uint32_t>(); break;
            case ParamType::UInt64:  args["return"] = ctx.returnValue<uint64_t>(); break;
            case ParamType::Float32: args["return"] = ctx.returnValue<float>(); break;
            case ParamType::Float64: args["return"] = ctx.returnValue<double>(); break;
            case ParamType::Pointer:
            default:
                args["return"] = std::format("{:#x}", ctx.returnValue<uint64_t>());
                break;
        }

        for (size_t i = 0; i < params.size(); i++) {
            if (!params[i].captureOnExit && !params[i].hasDeref) continue;
            auto p = extractTypedParam(ctx, static_cast<int>(i), params[i]);
            std::string key = p.value("name",
                std::string("arg") + std::to_string(i));
            if (params[i].hasDeref) key += "_out";
            if (p.contains("value"))
                args[key] = p["value"];
            else if (p.contains("value_hex"))
                args[key] = p["value_hex"];
        }
    }
};

}
