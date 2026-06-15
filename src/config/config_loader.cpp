#include "config/config_loader.h"
#include "logging/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace hookfw {

std::optional<FrameworkConfig> ConfigLoader::load(const std::string& filePath) {
    LOG_INFO("Loading config from: {}", filePath);

    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_ERROR("Cannot open config file: {}", filePath);
        return std::nullopt;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const nlohmann::json::parse_error& ex) {
        LOG_ERROR("JSON parse error in {}: {}", filePath, ex.what());
        return std::nullopt;
    }

    FrameworkConfig config;

    try {
        if (root.contains("output_dir")) {
            config.outputDir = root["output_dir"].get<std::string>();
        }
        if (root.contains("log_file")) {
            config.logFile = root["log_file"].get<std::string>();
        }
        if (root.contains("log_level")) {
            config.logLevel = root["log_level"].get<std::string>();
        }
        if (root.contains("output_format")) {
            std::string fmt = root["output_format"].get<std::string>();
            if (fmt == "perfetto") {
                config.outputFormat = OutputFormat::Perfetto;
            } else {
                config.outputFormat = OutputFormat::File;
            }
        }

        if (!root.contains("hooks") || !root["hooks"].is_array()) {
            LOG_ERROR("Config must contain 'hooks' array");
            return std::nullopt;
        }

        for (const auto& hookJson : root["hooks"]) {
            HookDescriptor desc;

            if (!hookJson.contains("module")) {
                LOG_ERROR("Hook entry missing 'module' field");
                return std::nullopt;
            }
            desc.target.moduleName = hookJson["module"].get<std::string>();

            if (hookJson.contains("symbol")) {
                desc.target.symbolName = hookJson["symbol"].get<std::string>();
                desc.target.useRva = false;
            } else if (hookJson.contains("rva")) {
                std::string rvaStr = hookJson["rva"].get<std::string>();
                desc.target.rva = std::stoull(rvaStr, nullptr, 16);
                desc.target.useRva = true;
            } else {
                LOG_ERROR("Hook entry must have 'symbol' or 'rva'");
                return std::nullopt;
            }

            if (hookJson.contains("action")) {
                desc.action = parseAction(hookJson["action"].get<std::string>());
            }

            if (hookJson.contains("mode")) {
                std::string m = hookJson["mode"].get<std::string>();
                desc.mode = (m == "capture") ? HookMode::Capture : HookMode::Trace;
            } else if (config.outputFormat == OutputFormat::Perfetto) {
                desc.mode = HookMode::Trace;
            }

            if (hookJson.contains("collector")) {
                desc.collectorType = parseCollectorType(hookJson["collector"].get<std::string>());
            }

            if (hookJson.contains("custom_collector_name")) {
                desc.customCollectorName = hookJson["custom_collector_name"].get<std::string>();
            }

            if (hookJson.contains("params") && hookJson["params"].is_array()) {
                for (const auto& paramJson : hookJson["params"]) {
                    ParamDescriptor param;
                    param.name = paramJson.value("name", "");
                    param.type = parseParamType(paramJson.value("type", "int64"));
                    param.bufferSize = paramJson.value("buffer_size", 0u);
                    if (paramJson.contains("deref_type")) {
                        param.hasDeref = true;
                        param.derefType = parseParamType(paramJson["deref_type"].get<std::string>());
                    }
                    desc.params.push_back(param);
                }
                if (desc.collectorType == CollectorType::Raw && !desc.params.empty()) {
                    desc.collectorType = CollectorType::Typed;
                }
            }

            if (hookJson.contains("return_type")) {
                desc.returnType = parseParamType(hookJson["return_type"].get<std::string>());
            }

            if (hookJson.contains("replace_module")) {
                desc.replacerModule = hookJson["replace_module"].get<std::string>();
            }
            if (hookJson.contains("replace_symbol")) {
                desc.replacerSymbol = hookJson["replace_symbol"].get<std::string>();
            }

            config.hooks.push_back(std::move(desc));
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("Error parsing config: {}", ex.what());
        return std::nullopt;
    }

    LOG_INFO("Config loaded: {} hooks defined", config.hooks.size());
    return config;
}

ParamType ConfigLoader::parseParamType(const std::string& typeStr) const {
    if (typeStr == "int8") return ParamType::Int8;
    if (typeStr == "int16") return ParamType::Int16;
    if (typeStr == "int32") return ParamType::Int32;
    if (typeStr == "int64") return ParamType::Int64;
    if (typeStr == "uint8") return ParamType::UInt8;
    if (typeStr == "uint16") return ParamType::UInt16;
    if (typeStr == "uint32") return ParamType::UInt32;
    if (typeStr == "uint64") return ParamType::UInt64;
    if (typeStr == "float32") return ParamType::Float32;
    if (typeStr == "float64") return ParamType::Float64;
    if (typeStr == "pointer") return ParamType::Pointer;
    if (typeStr == "cstring") return ParamType::CString;
    if (typeStr == "wstring") return ParamType::WString;
    if (typeStr == "wstring_ref") return ParamType::WStringRef;
    if (typeStr == "cstring_ref") return ParamType::CStringRef;
    if (typeStr == "buffer") return ParamType::Buffer;
    LOG_WARN("Unknown param type '{}', defaulting to int64", typeStr);
    return ParamType::Int64;
}

HookAction ConfigLoader::parseAction(const std::string& actionStr) const {
    if (actionStr == "capture") return HookAction::Capture;
    if (actionStr == "replace") return HookAction::Replace;
    if (actionStr == "capture_and_replace") return HookAction::CaptureAndReplace;
    LOG_WARN("Unknown action '{}', defaulting to capture", actionStr);
    return HookAction::Capture;
}

CollectorType ConfigLoader::parseCollectorType(const std::string& typeStr) const {
    if (typeStr == "raw") return CollectorType::Raw;
    if (typeStr == "typed") return CollectorType::Typed;
    if (typeStr == "custom") return CollectorType::Custom;
    LOG_WARN("Unknown collector type '{}', defaulting to raw", typeStr);
    return CollectorType::Raw;
}

}
