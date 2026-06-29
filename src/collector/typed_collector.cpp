#include "collector/typed_collector.h"
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

void TypedCollector::setParamDescriptors(const std::vector<ParamDescriptor>& params) {
    params_ = params;
}

void TypedCollector::setReturnType(ParamType type) {
    returnType_ = type;
}

void TypedCollector::onEntry(const HookContext& ctx) {
    if (!outputWriter_) {
        LOG_ERROR("TypedCollector::onEntry: no output writer set");
        return;
    }

    try {
        static int diagCount = 0;
        if (++diagCount <= 3) {
            LOG_INFO("TypedCollector::onEntry hook_id={} func={} params_count={}",
                     ctx.hookId, functionName_, params_.size());
        }
        auto* fileWriter = dynamic_cast<FileOutputWriter*>(outputWriter_.get());
        uint64_t seq = fileWriter ? fileWriter->nextSeq() : 0;

        nlohmann::json j;
        j["hook_id"] = ctx.hookId;

        nlohmann::json paramsJson = nlohmann::json::array();
        for (int i = 0; i < static_cast<int>(params_.size()); ++i) {
            try {
                paramsJson.push_back(extractParam(ctx, i, params_[i]));
            } catch (...) {
                nlohmann::json err;
                err["name"] = params_[i].name;
                err["error"] = "extract_failed";
                paramsJson.push_back(err);
                LOG_WARN("extractParam[{}] hook_id={} failed for '{}', B event preserved",
                         i, ctx.hookId, params_[i].name);
            }
        }
        j["params"] = paramsJson;

        // Capture archive buf_pos before function execution
        for (int i = 0; i < static_cast<int>(params_.size()); ++i) {
            if (params_[i].type == ParamType::su_CArchivePtr) {
                auto* archive = ctx.arg<void*>(i);
                if (archive && !IsBadReadPtr(archive, 0x70)) {
                    const_cast<HookContext&>(ctx).archiveBufPosBefore =
                        *reinterpret_cast<uint64_t*>((uint8_t*)archive + 0x38)
                        - *reinterpret_cast<uint64_t*>((uint8_t*)archive + 0x48);
                }
            }
        }

        if (!outputWriter_->writeJson(moduleName_, functionName_, seq, "entry", j)) {
            LOG_ERROR("TypedCollector: failed to write entry JSON for {}!{}",
                      moduleName_, functionName_);
        }

        outputWriter_->writeRaw(moduleName_, functionName_, seq, "entry",
                                reinterpret_cast<const uint8_t*>(&ctx), sizeof(HookContext));
    } catch (const std::exception& ex) {
        LOG_ERROR("TypedCollector::onEntry exception: {}", ex.what());
    }
}

void TypedCollector::onExit(const HookContext& ctx) {
    if (!outputWriter_) {
        LOG_ERROR("TypedCollector::onExit: no output writer set");
        return;
    }

    try {
        auto* fileWriter = dynamic_cast<FileOutputWriter*>(outputWriter_.get());
        uint64_t seq = fileWriter ? fileWriter->nextSeq() : 0;

        nlohmann::json j;
        j["return_value"] = extractReturnValue(ctx);

        nlohmann::json exitParams = nlohmann::json::array();
        for (int i = 0; i < static_cast<int>(params_.size()); ++i) {
            if (params_[i].captureOnExit) {
                try {
                    exitParams.push_back(extractParam(ctx, i, params_[i]));
                } catch (const std::exception& ex) {
                    LOG_ERROR("extractParam[{}] hook_id={} (exit) exception: {}", i, ctx.hookId, ex.what());
                    nlohmann::json err;
                    err["name"] = params_[i].name;
                    err["error"] = "extract_failed";
                    exitParams.push_back(err);
                } catch (...) {
                    LOG_ERROR("extractParam[{}] hook_id={} (exit) unknown exception", i, ctx.hookId);
                    nlohmann::json err;
                    err["name"] = params_[i].name;
                    err["error"] = "extract_failed";
                    exitParams.push_back(err);
                }
            }
        }
        if (!exitParams.empty()) {
            j["exit_params"] = exitParams;
        }

        nlohmann::json outputParams = nlohmann::json::array();
        for (int i = 0; i < static_cast<int>(params_.size()); ++i) {
            if (!params_[i].hasDeref) continue;
            try {
                auto ptr = ctx.arg<void*>(i);
                if (!ptr) continue;
                if (IsBadReadPtr(ptr, 8)) {
                    LOG_WARN("deref param[{}] hook_id={}: unreadable at {:p}",
                             i, ctx.hookId, static_cast<const void*>(ptr));
                    continue;
                }

                nlohmann::json op;
                op["name"] = params_[i].name;
                op["index"] = i;
                switch (params_[i].derefType) {
                    case ParamType::Int8:
                        op["deref_value"] = *reinterpret_cast<int8_t*>(ptr); break;
                    case ParamType::UInt8:
                        op["deref_value"] = *reinterpret_cast<uint8_t*>(ptr); break;
                    case ParamType::Int16:
                        op["deref_value"] = *reinterpret_cast<int16_t*>(ptr); break;
                    case ParamType::UInt16:
                        op["deref_value"] = *reinterpret_cast<uint16_t*>(ptr); break;
                    case ParamType::Int32:
                        op["deref_value"] = *reinterpret_cast<int32_t*>(ptr); break;
                    case ParamType::UInt32:
                        op["deref_value"] = *reinterpret_cast<uint32_t*>(ptr); break;
                    case ParamType::Int64:
                        op["deref_value"] = *reinterpret_cast<int64_t*>(ptr); break;
                    case ParamType::UInt64:
                        op["deref_value"] = *reinterpret_cast<uint64_t*>(ptr); break;
                    case ParamType::Float32:
                        op["deref_value"] = *reinterpret_cast<float*>(ptr); break;
                    case ParamType::Float64:
                        op["deref_value"] = *reinterpret_cast<double*>(ptr); break;
                    case ParamType::CStringRef:
                    case ParamType::CString: {
                        auto pStr = *reinterpret_cast<const char* const*>(ptr);
                        if (pStr && !IsBadReadPtr(pStr, 1)) {
                            size_t len = strnlen(pStr, 256);
                            op["deref_value"] = std::string(pStr, len);
                        } else {
                            op["deref_value"] = nullptr;
                        }
                        break;
                    }
                    case ParamType::WStringRef:
                    case ParamType::WString: {
                        auto pStr = *reinterpret_cast<const wchar_t* const*>(ptr);
                        if (pStr && !IsBadReadPtr(pStr, 2)) {
                            int len = WideCharToMultiByte(CP_UTF8, 0, pStr, -1, nullptr, 0, nullptr, nullptr);
                            if (len > 1) {
                                std::string utf8(len - 1, '\0');
                                WideCharToMultiByte(CP_UTF8, 0, pStr, -1, &utf8[0], len, nullptr, nullptr);
                                op["deref_value"] = utf8;
                            } else {
                                op["deref_value"] = "";
                            }
                        } else {
                            op["deref_value"] = nullptr;
                        }
                        break;
                    }
                    case ParamType::Buffer: {
                        auto pBuf = *reinterpret_cast<const uint8_t* const*>(ptr);
                        uint32_t size = params_[i].bufferSize > 0 ? params_[i].bufferSize : 64;
                        if (pBuf && !IsBadReadPtr(pBuf, size) && size > 0) {
                            size_t len = strnlen(reinterpret_cast<const char*>(pBuf), size);
                            op["deref_value"] = std::string(reinterpret_cast<const char*>(pBuf), len);
                        } else {
                            op["deref_value"] = nullptr;
                        }
                        break;
                    }
                    default:
                        op["deref_value"] = std::format("{:#x}", *reinterpret_cast<uint64_t*>(ptr)); break;
                }
                outputParams.push_back(op);
            } catch (...) {
                LOG_WARN("deref param[{}] hook_id={}: AV, E event preserved", i, ctx.hookId);
            }
        }
        if (!outputParams.empty()) {
            j["output_params"] = outputParams;
        }

        onExitCustom(ctx, j);

        if (!outputWriter_->writeJson(moduleName_, functionName_, seq, "exit", j)) {
            LOG_ERROR("TypedCollector: failed to write exit JSON for {}!{}",
                      moduleName_, functionName_);
        }

        outputWriter_->writeRaw(moduleName_, functionName_, seq, "exit",
                                reinterpret_cast<const uint8_t*>(&ctx), sizeof(HookContext));
    } catch (const std::exception& ex) {
        LOG_ERROR("TypedCollector::onExit exception: {}", ex.what());
    }
}

nlohmann::json TypedCollector::extractParam(const HookContext& ctx, int index,
                                            const ParamDescriptor& desc) const {
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
        case ParamType::CString: {
            param["type"] = "cstring";
            auto ptr = ctx.arg<const char*>(index);
            try {
                if (ptr && !IsBadReadPtr(ptr, 1)) {
                    param["value"] = std::string(ptr, strnlen(ptr, 1024));
                } else {
                    param["value"] = nullptr;
                    if (ptr) {
                        LOG_WARN("CString param[{}] hook_id={}: unreadable ptr {:p}",
                                 index, ctx.hookId, static_cast<const void*>(ptr));
                    }
                }
            } catch (...) {
                param["value"] = "<unreadable>";
                LOG_WARN("CString param[{}] hook_id={}: AV", index, ctx.hookId);
            }
            break;
        }
        case ParamType::WString: {
            param["type"] = "wstring";
            auto ptr = ctx.arg<const wchar_t*>(index);
            try {
                size_t len = 0;
                if (ptr && isLikelyWideString(ptr, &len) && len > 0) {
                    std::string utf8 = wideToUtf8(ptr, len);
                    if (!utf8.empty()) {
                        param["value"] = utf8;
                    } else {
                        param["value"] = std::format("<non-text@{:#x}>",
                                                     reinterpret_cast<uint64_t>(ptr));
                        LOG_WARN("WString param[{}] hook_id={}: UTF-8 conversion failed at {:p}",
                                 index, ctx.hookId, static_cast<const void*>(ptr));
                    }
                } else if (ptr && !IsBadReadPtr(ptr, 1)) {
                    auto charPtr = reinterpret_cast<const char*>(ptr);
                    size_t slen = strnlen(charPtr, 256);
                    if (slen > 0) {
                        param["value"] = std::string(charPtr, slen);
                        param["type"] = "string_fallback";
                    } else {
                        param["value"] = ptr
                            ? std::format("<non-text@{:#x}>", reinterpret_cast<uint64_t>(ptr))
                            : nullptr;
                        if (ptr) {
                            LOG_WARN("WString param[{}] hook_id={}: not a valid wide string or string at {:p}",
                                     index, ctx.hookId, static_cast<const void*>(ptr));
                        }
                    }
                } else {
                    param["value"] = ptr
                        ? std::format("<non-text@{:#x}>", reinterpret_cast<uint64_t>(ptr))
                        : nullptr;
                }
            } catch (...) {
                param["value"] = "<unreadable>";
                LOG_WARN("WString param[{}] hook_id={}: AV", index, ctx.hookId);
            }
            break;
        }
        case ParamType::WStringRef: {
            param["type"] = "wstring_ref";
            auto pCString = ctx.arg<const void*>(index);
            try {
                if (pCString && !IsBadReadPtr(pCString, 8)) {
                    auto pStr = *reinterpret_cast<const wchar_t* const*>(pCString);
                    size_t len = 0;
                    if (pStr && isLikelyWideString(pStr, &len) && len > 0) {
                        std::string utf8 = wideToUtf8(pStr, len);
                        if (!utf8.empty()) {
                            param["value"] = utf8;
                        } else {
                            param["value"] = std::format("<non-text@{:#x}>",
                                                         reinterpret_cast<uint64_t>(pStr));
                            LOG_WARN("WStringRef param[{}] hook_id={}: UTF-8 conversion failed at {:p}",
                                     index, ctx.hookId, static_cast<const void*>(pStr));
                        }
                    } else if (pStr && !IsBadReadPtr(pStr, 1)) {
                        auto charPtr = reinterpret_cast<const char*>(pStr);
                        size_t slen = strnlen(charPtr, 256);
                        if (slen > 0) {
                            param["value"] = std::string(charPtr, slen);
                            param["type"] = "string_fallback";
                        } else {
                            param["value"] = pStr
                                ? std::format("<non-text@{:#x}>", reinterpret_cast<uint64_t>(pStr))
                                : nullptr;
                            if (pStr) {
                                LOG_WARN("WStringRef param[{}] hook_id={}: invalid wide string at {:p}",
                                         index, ctx.hookId, static_cast<const void*>(pStr));
                            }
                        }
                    } else {
                        param["value"] = pStr
                            ? std::format("<non-text@{:#x}>", reinterpret_cast<uint64_t>(pStr))
                            : nullptr;
                    }
                } else {
                    param["value"] = nullptr;
                    if (pCString) {
                        LOG_WARN("WStringRef param[{}] hook_id={}: unreadable CString ref at {:p}",
                                 index, ctx.hookId, static_cast<const void*>(pCString));
                    }
                }
            } catch (...) {
                param["value"] = "<unreadable>";
                LOG_WARN("WStringRef param[{}] hook_id={}: AV", index, ctx.hookId);
            }
            break;
        }
        case ParamType::CStringRef: {
            param["type"] = "cstring_ref";
            auto pObj = ctx.arg<const void*>(index);
            try {
                if (!pObj || IsBadReadPtr(pObj, 8)) {
                    param["value"] = pObj
                        ? std::format("{:#x}", reinterpret_cast<uint64_t>(pObj))
                        : nullptr;
                    if (pObj) {
                        LOG_WARN("CStringRef param[{}] hook_id={}: unreadable object at {:p}",
                                 index, ctx.hookId, static_cast<const void*>(pObj));
                    }
                    break;
                }
                auto pStr = *reinterpret_cast<const char* const*>(pObj);
                if (!pStr) { param["value"] = nullptr; break; }

                MEMORY_BASIC_INFORMATION mbi = {};
                if (!VirtualQuery(pStr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {
                    param["value"] = std::format("<badptr@{:#x}>", reinterpret_cast<uint64_t>(pStr));
                    LOG_WARN("CStringRef param[{}] hook_id={}: invalid string ptr at {:p}",
                             index, ctx.hookId, static_cast<const void*>(pStr));
                    break;
                }
                auto regionEnd = reinterpret_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
                size_t safeBytes = regionEnd - reinterpret_cast<const uint8_t*>(pStr);
                size_t maxLen = (safeBytes > 256) ? 256 : safeBytes;
                size_t len = strnlen(pStr, maxLen);
                param["value"] = std::string(pStr, len);
            } catch (...) {
                param["value"] = "<unreadable>";
                LOG_WARN("CStringRef param[{}] hook_id={}: AV", index, ctx.hookId);
            }
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
            try {
                if (ptr && !IsBadReadPtr(ptr, size) && size > 0) {
                    size_t len = strnlen(reinterpret_cast<const char*>(ptr), size);
                    param["value"] = std::string(reinterpret_cast<const char*>(ptr), len);
                    param["type"] = "buffer_as_string";
                } else {
                    param["value"] = nullptr;
                    if (ptr) {
                        LOG_WARN("Buffer param[{}] hook_id={}: unreadable at {:p} size={}",
                                 index, ctx.hookId, static_cast<const void*>(ptr), size);
                    }
                }
            } catch (...) {
                param["value"] = "<unreadable>";
                LOG_WARN("Buffer param[{}] hook_id={}: AV", index, ctx.hookId);
            }
            break;
        }
        case ParamType::su_CArchivePtr: {
            param["type"] = "su_CArchive";
            auto* archive = ctx.arg<void*>(index);
            if (archive && !IsBadReadPtr(archive, 0x70)) {
                uint64_t bufCur   = *reinterpret_cast<uint64_t*>((uint8_t*)archive + 0x38);
                uint64_t bufStart = *reinterpret_cast<uint64_t*>((uint8_t*)archive + 0x48);
                param["buf_pos_before"] = ctx.archiveBufPosBefore;
                param["buf_pos_after"]  = bufCur - bufStart;
                param["counter"] = *reinterpret_cast<uint32_t*>((uint8_t*)archive + 0x50);
            } else {
                param["value"] = std::format("0x{:016x}", reinterpret_cast<uint64_t>(archive));
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
        case ParamType::Variant:
        case ParamType::SafeArray: {
            param["type"] = (desc.type == ParamType::Variant) ? "variant" : "safearray";
            auto* ptr = ctx.arg<void*>(index);
            param["value"] = std::format("0x{:016x}", reinterpret_cast<uint64_t>(ptr));
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
        case ParamType::UInt32:
            ret["type"] = "uint32";
            ret["value"] = ctx.returnValue<uint32_t>();
            break;
        case ParamType::UInt64:
            ret["type"] = "uint64";
            ret["value"] = ctx.returnValue<uint64_t>();
            break;
        case ParamType::Pointer:
            ret["type"] = "pointer";
            ret["value"] = std::format("{:#x}", ctx.returnValue<uint64_t>());
            break;
        default:
            ret["type"] = "int64";
            ret["value"] = ctx.returnValue<int64_t>();
            break;
    }
    return ret;
}

}
