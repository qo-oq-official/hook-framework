#pragma once
#include "config/hook_config.h"
#include <string>
#include <optional>

namespace hookfw {

class ConfigLoader {
public:
    std::optional<FrameworkConfig> load(const std::string& filePath);

private:
    ParamType parseParamType(const std::string& typeStr) const;
    HookAction parseAction(const std::string& actionStr) const;
    CollectorType parseCollectorType(const std::string& typeStr) const;
};

}
