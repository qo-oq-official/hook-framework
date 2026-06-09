#pragma once
#include "config/hook_config.h"
#include <string>
#include <vector>

namespace hookfw {

class ConfigValidator {
public:
    bool validate(const FrameworkConfig& config);
    const std::vector<std::string>& errors() const { return errors_; }

private:
    std::vector<std::string> errors_;
};

}
