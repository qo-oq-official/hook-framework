#include "config/config_loader.h"
#include "logging/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_map>
#include <string_view>

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
            } else {
                desc.mode = HookMode::Trace;
            }

            if (hookJson.contains("collector")) {
                desc.collectorType = parseCollectorType(hookJson["collector"].get<std::string>());
            }

            if (hookJson.contains("custom_collector_name")) {
                desc.customCollectorName = hookJson["custom_collector_name"].get<std::string>();
            }

            if (hookJson.contains("injector")) {
                desc.injectorName = hookJson["injector"].get<std::string>();
            }
            if (hookJson.contains("injector_mode")) {
                std::string m = hookJson["injector_mode"].get<std::string>();
                desc.injectorMode = (m == "post") ? InjectorMode::Post : InjectorMode::Pre;
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
                    if (paramJson.contains("capture_on_exit")) {
                        param.captureOnExit = paramJson["capture_on_exit"].get<bool>();
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
            if (hookJson.contains("display_name")) {
                desc.displayName = hookJson["display_name"].get<std::string>();
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
    static const std::unordered_map<std::string_view, ParamType> kMap = {
        {"int8", ParamType::Int8},   {"int16", ParamType::Int16},
        {"int32", ParamType::Int32}, {"int64", ParamType::Int64},
        {"uint8", ParamType::UInt8}, {"uint16", ParamType::UInt16},
        {"uint32", ParamType::UInt32}, {"uint64", ParamType::UInt64},
        {"float32", ParamType::Float32}, {"float64", ParamType::Float64},
        {"pointer", ParamType::Pointer},
        {"cstring", ParamType::CString}, {"wstring", ParamType::WString},
        {"wstring_ref", ParamType::WStringRef}, {"cstring_ref", ParamType::CStringRef},
        {"buffer", ParamType::Buffer},
        {"su_CArchivePtr", ParamType::su_CArchivePtr},
        {"bstr", ParamType::BStr},
        {"bool", ParamType::Bool},
        {"BinStreamPtr", ParamType::BinStreamPtr},
    };
    auto it = kMap.find(typeStr);
    if (it != kMap.end()) return it->second;
    LOG_WARN("Unknown param type '{}', defaulting to int64", typeStr);
    return ParamType::Int64;
}

HookAction ConfigLoader::parseAction(const std::string& actionStr) const {
    static const std::unordered_map<std::string_view, HookAction> kMap = {
        {"capture", HookAction::Capture},
        {"replace", HookAction::Replace},
        {"capture_and_replace", HookAction::CaptureAndReplace},
    };
    auto it = kMap.find(actionStr);
    if (it != kMap.end()) return it->second;
    LOG_WARN("Unknown action '{}', defaulting to capture", actionStr);
    return HookAction::Capture;
}

CollectorType ConfigLoader::parseCollectorType(const std::string& typeStr) const {
    static const std::unordered_map<std::string_view, CollectorType> kMap = {
        {"raw", CollectorType::Raw},
        {"typed", CollectorType::Typed},
        {"custom", CollectorType::Custom},
    };
    auto it = kMap.find(typeStr);
    if (it != kMap.end()) return it->second;
    LOG_WARN("Unknown collector type '{}', defaulting to raw", typeStr);
    return CollectorType::Raw;
}

}
