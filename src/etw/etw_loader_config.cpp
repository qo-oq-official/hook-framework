#include "etw/etw_loader_config.h"

#include "logging.h"
#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>
#include <windows.h>

namespace hookfw {

namespace {

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) {
        return L"";
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::wstring toAbsolute(const std::wstring& p, const std::filesystem::path& baseDir) {
    std::filesystem::path path(p);
    if (path.is_absolute()) {
        return path.lexically_normal().wstring();
    }
    return (baseDir / path).lexically_normal().wstring();
}

}  // namespace

std::optional<EtwLoaderConfig> EtwLoaderConfigLoader::load(const std::string& filePath) {
    spdlog::info("Loading ETW loader config from: {}", filePath);

    std::ifstream file(filePath);
    if (!file.is_open()) {
        spdlog::error("Cannot open config file: {}", filePath);
        return std::nullopt;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const nlohmann::json::parse_error& ex) {
        spdlog::error("JSON parse error in {}: {}", filePath, ex.what());
        return std::nullopt;
    }

    EtwLoaderConfig config;
    std::filesystem::path configDir = std::filesystem::path(filePath).parent_path();

    try {
        if (root.contains("session_name") && root["session_name"].is_string()) {
            config.sessionName = utf8ToWide(root["session_name"].get<std::string>());
        }

        if (!root.contains("rules") || !root["rules"].is_array()) {
            spdlog::error("Config must contain a 'rules' array");
            return std::nullopt;
        }

        for (const auto& r : root["rules"]) {
            EtwLoaderRule rule;

            if (r.contains("on_load") && r["on_load"].is_array()) {
                for (const auto& b : r["on_load"]) {
                    rule.onLoad.push_back(utf8ToWide(b.get<std::string>()));
                }
            }
            if (r.contains("inject") && r["inject"].is_array()) {
                for (const auto& c : r["inject"]) {
                    rule.inject.push_back(toAbsolute(utf8ToWide(c.get<std::string>()), configDir));
                }
            }

            if (rule.onLoad.empty() || rule.inject.empty()) {
                spdlog::error(L"Must have non-empty 'on_load' and 'inject'");
                return std::nullopt;
            }

            config.rules.push_back(std::move(rule));
        }
    } catch (const std::exception& ex) {
        spdlog::error("Error parsing config: {}", ex.what());
        return std::nullopt;
    }

    spdlog::info("ETW loader config loaded: {} rule(s)", config.rules.size());
    return config;
}

}  // namespace hookfw
