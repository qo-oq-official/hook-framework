#include "collector/typed_collector.h"
#include "logging/logger.h"
#include <format>
#include <Windows.h>

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
        auto* fileWriter = dynamic_cast<FileOutputWriter*>(outputWriter_.get());
        uint64_t seq = fileWriter ? fileWriter->nextSeq() : 0;

        nlohmann::json j;
        j["hook_id"] = ctx.hookId;
        j["function"] = functionName_;
        j["module"] = moduleName_;
        j["phase"] = "entry";

        nlohmann::json paramsJson = nlohmann::json::array();
        for (int i = 0; i < static_cast<int>(params_.size()); ++i) {
            paramsJson.push_back(extractParam(ctx, i, params_[i]));
        }
        j["params"] = paramsJson;

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
        j["hook_id"] = ctx.hookId;
        j["function"] = functionName_;
        j["module"] = moduleName_;
        j["phase"] = "exit";
        j["return_value"] = extractReturnValue(ctx);

        nlohmann::json exitParams = nlohmann::json::array();
        for (int i = 0; i < static_cast<int>(params_.size()); ++i) {
            if (params_[i].captureOnExit) {
                exitParams.push_back(extractParam(ctx, i, params_[i]));
            }
        }
        if (!exitParams.empty()) {
            j["exit_params"] = exitParams;
        }

        nlohmann::json outputParams = nlohmann::json::array();
        for (int i = 0; i < static_cast<int>(params_.size()); ++i) {
            if (!params_[i].hasDeref) continue;
            auto ptr = ctx.arg<void*>(i);
            if (!ptr || IsBadReadPtr(ptr, 8)) continue;

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
                default:
                    op["deref_value"] = std::format("{:#x}", *reinterpret_cast<uint64_t*>(ptr)); break;
            }
            outputParams.push_back(op);
        }
        if (!outputParams.empty()) {
            j["output_params"] = outputParams;
        }

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
                }
            } catch (...) {
                param["value"] = "<unreadable>";
                LOG_WARN("Failed to read CString param at index {}", index);
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
                        LOG_TRACE("WString param[{}] at {:p} failed UTF-8 conversion",
                                  index, static_cast<const void*>(ptr));
                    }
                } else {
                    param["value"] = ptr
                        ? std::format("<non-text@{:#x}>", reinterpret_cast<uint64_t>(ptr))
                        : nullptr;
                }
            } catch (...) {
                param["value"] = "<unreadable>";
                LOG_WARN("Failed to read WString param at index {}", index);
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
                            LOG_TRACE("WStringRef param[{}] deref {:p} failed UTF-8 conversion",
                                      index, static_cast<const void*>(pStr));
                        }
                    } else {
                        param["value"] = pStr
                            ? std::format("<non-text@{:#x}>", reinterpret_cast<uint64_t>(pStr))
                            : nullptr;
                    }
                } else {
                    param["value"] = nullptr;
                }
            } catch (...) {
                param["value"] = "<unreadable>";
                LOG_WARN("Failed to read WStringRef param at index {}", index);
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
            } catch (...) {
                param["value"] = "<unreadable>";
                LOG_TRACE("CStringRef: AV for hook_id={} index={}", ctx.hookId, index);
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
                    LOG_TRACE("Buffer exit: ptr={:#x} size={} idx={}", 
                              reinterpret_cast<uint64_t>(ptr), size, index);
                    bool displayed = false;
                    if (size % 2 == 0 && size >= 2) {
                        auto wptr = reinterpret_cast<const wchar_t*>(ptr);
                        size_t wlen = size / 2;
                        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wptr, static_cast<int>(wlen),
                                                         nullptr, 0, nullptr, nullptr);
                        if (utf8Len > 0 && utf8Len < 4096) {
                            std::string utf8(utf8Len, '\0');
                            WideCharToMultiByte(CP_UTF8, 0, wptr, static_cast<int>(wlen),
                                               utf8.data(), utf8Len, nullptr, nullptr);
                            param["value"] = utf8;
                            param["type"] = "buffer_as_wstring";
                            displayed = true;
                        }
                    }
                    if (!displayed) {
                        size_t len = strnlen(reinterpret_cast<const char*>(ptr), size);
                        param["value"] = std::string(reinterpret_cast<const char*>(ptr), len);
                        param["type"] = "buffer_as_ansi";
                    }
                } else {
                    param["value"] = nullptr;
                }
            } catch (...) {
                param["value"] = "<unreadable>";
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
