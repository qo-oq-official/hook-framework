#include "collector/typed_collector.h"
#include "output/hook_data_source.h"
#include "logging/logger.h"
#include <format>
#include <Windows.h>
#include <OleAuto.h>

namespace hookfw {

static bool isLikelyWideString(const wchar_t* ptr, size_t* outLen) {
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(ptr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {
        return false;
    }
    auto regionEnd = reinterpret_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
    size_t safeBytes = regionEnd - reinterpret_cast<const uint8_t*>(ptr);
    size_t maxChars = safeBytes / sizeof(wchar_t);
    if (maxChars == 0) return false;
    if (maxChars > 256) maxChars = 256;

    size_t len = wcsnlen(ptr, maxChars);

    size_t checkLen = (len < 8) ? len : 8;
    for (size_t i = 0; i < checkLen; i++) {
        wchar_t c = ptr[i];
        if (c >= 0xD800 && c <= 0xDFFF) return false;
        if (c == 0xFFFE || c == 0xFFFF) return false;
        if (c < 0x0020 && c != L'\t' && c != L'\n' && c != L'\r') return false;
    }

    *outLen = len;
    return true;
}

static std::string wideToUtf8(const wchar_t* ptr, size_t len) {
    if (len == 0) return "";
    int utf8Len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                      ptr, static_cast<int>(len),
                                      nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return "";
    std::string utf8(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                        ptr, static_cast<int>(len),
                        utf8.data(), utf8Len, nullptr, nullptr);
    return utf8;
}

static nlohmann::json extractDerefValue(void* ptr, ParamType derefType);

void TypedCollector::setParamDescriptors(const std::vector<ParamDescriptor>& params) {
    params_ = params;
}

void TypedCollector::setReturnType(ParamType type) {
    returnType_ = type;
}

void TypedCollector::onEntry(const HookContext& ctx) {
    try {
        nlohmann::json argsJson = nlohmann::json::array();
        for (int i = 0; i < static_cast<int>(params_.size()); ++i) {
            try {
                auto p = extractParam(ctx, i, params_[i], ExtractionPhase::Entry);
                if (params_[i].hasDeref) {
                    auto ptr = ctx.arg<void*>(i);
                    if (ptr && !IsBadReadPtr(ptr, 8)) {
                        p["deref"] = extractDerefValue(ptr, params_[i].derefType);
                    }
                }
                argsJson.push_back(p);
            } catch (...) {
                nlohmann::json err;
                err["name"] = params_[i].name;
                err["error"] = "extract_failed";
                argsJson.push_back(err);
            }
        }
        nlohmann::json customData;
        onEntryCustom(ctx, customData);

        nlohmann::json perfettoArgs;
        buildPerfettoArgs(argsJson, customData, perfettoArgs["Entry"]);
        HookDataSource::TraceSlice(functionName_, moduleName_, 'B',
                                    GetCurrentThreadId(), perfettoArgs);
    } catch (const std::exception& ex) {
        LOG_ERROR("TypedCollector::onEntry exception: {}", ex.what());
    }
}

void TypedCollector::onExit(const HookContext& ctx) {
    try {
        nlohmann::json returnValue = extractReturnValue(ctx);

        nlohmann::json argsJson = nlohmann::json::array();
        for (int i = 0; i < static_cast<int>(params_.size()); ++i) {
            try {
                auto p = extractParam(ctx, i, params_[i], ExtractionPhase::Exit);
                if (params_[i].hasDeref) {
                    auto ptr = ctx.arg<void*>(i);
                    if (ptr && !IsBadReadPtr(ptr, 8)) {
                        p["deref"] = extractDerefValue(ptr, params_[i].derefType);
                    }
                }
                argsJson.push_back(p);
            } catch (...) {
                nlohmann::json err;
                err["name"] = params_[i].name;
                err["error"] = "extract_failed";
                argsJson.push_back(err);
            }
        }
        nlohmann::json customData;
        onExitCustom(ctx, customData);

        nlohmann::json perfettoArgs;
        buildPerfettoArgs(argsJson, customData, perfettoArgs["Exit"]);
        perfettoArgs["return"] = returnValue;
        perfettoArgs["hook_id"] = ctx.hookId;
        HookDataSource::TraceSlice(functionName_, moduleName_, 'E',
                                    GetCurrentThreadId(), perfettoArgs);
    } catch (const std::exception& ex) {
        LOG_ERROR("TypedCollector::onExit exception: {}", ex.what());
    }
}

static nlohmann::json extractDerefValue(void* ptr, ParamType derefType) {
    if (!ptr) return nullptr;
    switch (derefType) {
        case ParamType::Int8:     return *reinterpret_cast<int8_t*>(ptr);
        case ParamType::UInt8:    return *reinterpret_cast<uint8_t*>(ptr);
        case ParamType::Int16:    return *reinterpret_cast<int16_t*>(ptr);
        case ParamType::UInt16:   return *reinterpret_cast<uint16_t*>(ptr);
        case ParamType::Int32:    return *reinterpret_cast<int32_t*>(ptr);
        case ParamType::UInt32:   return *reinterpret_cast<uint32_t*>(ptr);
        case ParamType::Int64:    return *reinterpret_cast<int64_t*>(ptr);
        case ParamType::UInt64:   return *reinterpret_cast<uint64_t*>(ptr);
        case ParamType::Float32:  return *reinterpret_cast<float*>(ptr);
        case ParamType::Float64:  return *reinterpret_cast<double*>(ptr);
        case ParamType::CStringRef:
        case ParamType::CString: {
            auto pStr = *reinterpret_cast<const char* const*>(ptr);
            if (pStr && !IsBadReadPtr(pStr, 1))
                return std::string(pStr, strnlen(pStr, 256));
            return nullptr;
        }
        case ParamType::WStringRef:
        case ParamType::WString: {
            auto pStr = *reinterpret_cast<const wchar_t* const*>(ptr);
            if (!pStr || IsBadReadPtr(pStr, 2)) return nullptr;
            int len = WideCharToMultiByte(CP_UTF8, 0, pStr, -1, nullptr, 0, nullptr, nullptr);
            if (len > 1) {
                std::string utf8(len - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, pStr, -1, &utf8[0], len, nullptr, nullptr);
                return utf8;
            }
            return nullptr;
        }
        case ParamType::Buffer: {
            auto pBuf = *reinterpret_cast<const uint8_t* const*>(ptr);
            if (!pBuf || IsBadReadPtr(pBuf, 64)) return nullptr;
            size_t len = strnlen(reinterpret_cast<const char*>(pBuf), 64);
            return std::string(reinterpret_cast<const char*>(pBuf), len);
        }
        default:
            return std::format("0x{:016x}", *reinterpret_cast<uint64_t*>(ptr));
    }
}

nlohmann::json TypedCollector::extractParam(const HookContext& ctx, int index,
                                            const ParamDescriptor& desc,
                                            ExtractionPhase /*phase*/) const {
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
            param["value"] = std::format("0x{:016x}", ctx.arg<uint64_t>(index));
            break;
        case ParamType::CString: {
            param["type"] = "cstring";
            auto ptr = ctx.arg<const char*>(index);
            if (ptr && !IsBadReadPtr(ptr, 1))
                param["value"] = std::string(ptr, strnlen(ptr, 1024));
            else
                param["value"] = nullptr;
            break;
        }
        case ParamType::WString: {
            param["type"] = "wstring";
            auto ptr = ctx.arg<const wchar_t*>(index);
            size_t len = 0;
            if (ptr && isLikelyWideString(ptr, &len) && len > 0) {
                param["value"] = wideToUtf8(ptr, len);
                if (param["value"].get<std::string>().empty())
                    param["value"] = std::format("<non-text@{:#x}>", reinterpret_cast<uintptr_t>(ptr));
            } else if (ptr && !IsBadReadPtr(ptr, 1)) {
                auto charPtr = reinterpret_cast<const char*>(ptr);
                size_t slen = strnlen(charPtr, 256);
                if (slen > 0)
                    param["value"] = std::string(charPtr, slen);
                else
                    param["value"] = std::format("<non-text@{:#x}>", reinterpret_cast<uintptr_t>(ptr));
            } else {
                param["value"] = nullptr;
            }
            break;
        }
        case ParamType::WStringRef: {
            param["type"] = "wstring_ref";
            auto pCString = ctx.arg<const void*>(index);
            if (pCString && !IsBadReadPtr(pCString, 8)) {
                auto pStr = *reinterpret_cast<const wchar_t* const*>(pCString);
                size_t len = 0;
                if (pStr && isLikelyWideString(pStr, &len) && len > 0) {
                    param["value"] = wideToUtf8(pStr, len);
                    if (param["value"].get<std::string>().empty())
                        param["value"] = std::format("<non-text@{:#x}>", reinterpret_cast<uintptr_t>(pStr));
                } else if (pStr && !IsBadReadPtr(pStr, 1)) {
                    auto charPtr = reinterpret_cast<const char*>(pStr);
                    size_t slen = strnlen(charPtr, 256);
                    if (slen > 0)
                        param["value"] = std::string(charPtr, slen);
                    else
                        param["value"] = std::format("<non-text@{:#x}>", reinterpret_cast<uintptr_t>(pStr));
                } else {
                    param["value"] = nullptr;
                }
            } else {
                param["value"] = nullptr;
            }
            break;
        }
        case ParamType::CStringRef: {
            param["type"] = "cstring_ref";
            auto pObj = ctx.arg<const void*>(index);
            if (!pObj || IsBadReadPtr(pObj, 8)) {
                param["value"] = pObj
                    ? std::format("{:#x}", reinterpret_cast<uint64_t>(pObj)) : nullptr;
                break;
            }
            auto pStr = *reinterpret_cast<const char* const*>(pObj);
            if (!pStr) { param["value"] = nullptr; break; }
            MEMORY_BASIC_INFORMATION mbi = {};
            if (!VirtualQuery(pStr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {
                param["value"] = std::format("<badptr@{:#x}>", reinterpret_cast<uint64_t>(pStr));
                break;
            }
            auto regionEnd = reinterpret_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
            size_t safeBytes = regionEnd - reinterpret_cast<const uint8_t*>(pStr);
            size_t maxLen = (safeBytes > 256) ? 256 : safeBytes;
            size_t len = strnlen(pStr, maxLen);
            param["value"] = std::string(pStr, len);
            break;
        }
        case ParamType::Buffer: {
            param["type"] = "buffer";
            auto ptr = ctx.arg<const uint8_t*>(index);
            uint32_t size = desc.bufferSize > 0 ? desc.bufferSize : 64;
            if (desc.bufferSize == 0 && index + 1 < static_cast<int>(params_.size())) {
                auto nextType = params_[index + 1].type;
                if (nextType == ParamType::UInt32 || nextType == ParamType::Int32) {
                    size = ctx.arg<uint32_t>(index + 1);
                    if (size > 2048) size = 256;
                }
            }
            if (ptr && !IsBadReadPtr(ptr, size) && size > 0) {
                size_t len = strnlen(reinterpret_cast<const char*>(ptr), size);
                param["value"] = std::string(reinterpret_cast<const char*>(ptr), len);
            } else {
                param["value"] = nullptr;
            }
            break;
        }
        case ParamType::su_CArchivePtr: {
            param["type"] = "su_CArchive";
            auto* archive = ctx.arg<void*>(index);
            param["value"] = std::format("0x{:016x}", reinterpret_cast<uint64_t>(archive));
            if (archive && !IsBadReadPtr(archive, 0x70)) {
                nlohmann::json state;
                uint64_t bufCur   = *reinterpret_cast<uint64_t*>((uint8_t*)archive + 0x38);
                uint64_t bufStart = *reinterpret_cast<uint64_t*>((uint8_t*)archive + 0x48);
                state["buf_pos"] = std::format("0x{:016x}", bufCur - bufStart);
                state["counter"] = *reinterpret_cast<uint32_t*>((uint8_t*)archive + 0x50);
                param["state"] = state;
            }
            break;
        }
        case ParamType::BStr: {
            param["type"] = "bstr";
            auto* bstr = ctx.arg<wchar_t*>(index);
            if (bstr && !IsBadReadPtr(bstr, 4)) {
                UINT len = SysStringLen(bstr);
                if (len > 0 && !IsBadReadPtr(bstr, static_cast<size_t>(len) * 2 + 2)) {
                    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, bstr, static_cast<int>(len),
                                                      nullptr, 0, nullptr, nullptr);
                    if (utf8Len > 0) {
                        std::string utf8(utf8Len, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, bstr, static_cast<int>(len),
                                            &utf8[0], utf8Len, nullptr, nullptr);
                        param["value"] = utf8;
                    } else {
                        param["value"] = std::format("<bstr@{:#x}>", reinterpret_cast<uint64_t>(bstr));
                    }
                } else {
                    param["value"] = nullptr;
                }
            } else {
                param["value"] = nullptr;
            }
            break;
        }
        case ParamType::Bool:
            param["type"] = "bool";
            param["value"] = (ctx.arg<uint64_t>(index) != 0);
            break;
        case ParamType::BinStreamPtr: {
            param["type"] = "BinStreamPtr";
            auto* stream = ctx.arg<void*>(index);
            param["value"] = std::format("0x{:016x}", reinterpret_cast<uint64_t>(stream));
            if (stream && !IsBadReadPtr(stream, 0x30)) {
                nlohmann::json state;
                uint64_t start = *reinterpret_cast<uint64_t*>((uint8_t*)stream + 0x20);
                uint64_t end   = *reinterpret_cast<uint64_t*>((uint8_t*)stream + 0x28);
                state["buf_pos"] = std::format("0x{:016x}", end - start);
                state["count"]   = *reinterpret_cast<uint32_t*>((uint8_t*)stream + 0x10);
                param["state"] = state;
            }
            break;
        }
    }

    return param;
}

nlohmann::json TypedCollector::extractReturnValue(const HookContext& ctx) const {
    nlohmann::json ret;
    switch (returnType_) {
        case ParamType::Int32:
            ret["type"] = "int32";
            ret["value"] = ctx.returnValue<int32_t>();
            break;
        case ParamType::Int64:
            ret["type"] = "int64";
            ret["value"] = ctx.returnValue<int64_t>();
            break;
        case ParamType::UInt8:
            ret["type"] = "uint8";
            ret["value"] = ctx.returnValue<uint8_t>();
            break;
        case ParamType::UInt16:
            ret["type"] = "uint16";
            ret["value"] = ctx.returnValue<uint16_t>();
            break;
        case ParamType::UInt32:
            ret["type"] = "uint32";
            ret["value"] = ctx.returnValue<uint32_t>();
            break;
        case ParamType::UInt64:
            ret["type"] = "uint64";
            ret["value"] = ctx.returnValue<uint64_t>();
            break;
        case ParamType::Float32:
            ret["type"] = "float32";
            ret["value"] = ctx.returnValue<float>();
            break;
        case ParamType::Float64:
            ret["type"] = "float64";
            ret["value"] = ctx.returnValue<double>();
            break;
        case ParamType::Pointer:
        case ParamType::su_CArchivePtr:
            ret["type"] = "pointer";
            ret["value"] = std::format("{:#x}", ctx.returnValue<uint64_t>());
            break;
        case ParamType::BStr:
            ret["type"] = "bstr";
            ret["value"] = std::format("0x{:016x}", ctx.returnValue<uint64_t>());
            break;
        default:
            ret["type"] = "int64";
            ret["value"] = ctx.returnValue<int64_t>();
            break;
    }
    return ret;
}

void TypedCollector::buildPerfettoArgs(const nlohmann::json& params,
                                        const nlohmann::json& customData,
                                        nlohmann::json& args) const {
    // for (const auto& p : params) {
    //     std::string key = p.value("name", "arg");
    //     flattenParam(p, key, args);
    // }
    // for (const auto& [key, val] : customData.items()) {
    //     args[key] = val;
    // }
    args["params"] = params;
    args["customData"] = customData;
}

void TypedCollector::flattenParam(const nlohmann::json& p, const std::string& key,
                                   nlohmann::json& args) {
    std::string prefix = (p.value("type", "") == "su_CArchive") ? "archive" : key;

    if (p.contains("value"))
        args[prefix] = p["value"];

    if (p.contains("state") && p["state"].is_object()) {
        for (const auto& [k, v] : p["state"].items())
            args[prefix + "_" + std::string(k)] = v;
    }

    if (p.contains("deref"))
        args[prefix + "_out"] = p["deref"];
}

}
