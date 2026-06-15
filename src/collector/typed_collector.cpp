#include "collector/typed_collector.h"
#include "logging/logger.h"
#include <format>
#include <Windows.h>

namespace hookfw {

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
                if (ptr && !IsBadReadPtr(ptr, 2)) {
                    size_t len = wcsnlen(ptr, 512);
                    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, ptr, static_cast<int>(len),
                                                     nullptr, 0, nullptr, nullptr);
                    if (utf8Len > 0) {
                        std::string utf8(utf8Len, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, ptr, static_cast<int>(len),
                                            utf8.data(), utf8Len, nullptr, nullptr);
                        param["value"] = utf8;
                    } else {
                        param["value"] = "";
                    }
                } else {
                    param["value"] = nullptr;
                }
            } catch (...) {
                param["value"] = "<unreadable>";
                LOG_WARN("Failed to read WString param at index {}", index);
            }
            break;
        }
        case ParamType::Buffer: {
            param["type"] = "buffer";
            auto ptr = ctx.arg<const uint8_t*>(index);
            uint32_t size = desc.bufferSize > 0 ? desc.bufferSize : 64;
            try {
                if (ptr && !IsBadReadPtr(ptr, size)) {
                    std::string hex;
                    for (uint32_t i = 0; i < size; ++i) {
                        hex += std::format("{:02x}", ptr[i]);
                        if (i + 1 < size) hex += ' ';
                    }
                    param["value_hex"] = hex;
                    param["size"] = size;
                } else {
                    param["value_hex"] = nullptr;
                }
            } catch (...) {
                param["value_hex"] = "<unreadable>";
                LOG_WARN("Failed to read Buffer param at index {}", index);
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
