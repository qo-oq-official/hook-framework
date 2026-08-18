#pragma once

#include <optional>
#include <string>
#include <vector>

namespace hookfw {

// One rule: when any image whose basename is in `onLoad` (B.dll) is loaded,
// inject every DLL in `inject` (C.dll) into the process that loaded it.
struct EtwLoaderRule {
    std::wstring onLoad;  // B.dll basenames (triggers)
    std::wstring inject;  // C.dll absolute paths (actions)
};

struct EtwLoaderConfig {
    std::wstring sessionName = L"HookFramework-DllLoader";
    std::vector<EtwLoaderRule> rules;
};

class EtwLoaderConfigLoader {
public:
    // Loads and validates a JSON config. Returns std::nullopt on failure.
    std::optional<EtwLoaderConfig> load(const std::string& filePath);
};

}  // namespace hookfw
