#include "config/config_validator.h"
#include "logging/logger.h"

namespace hookfw {

bool ConfigValidator::validate(const FrameworkConfig& config) {
    errors_.clear();

    if (config.outputDir.empty()) {
        errors_.push_back("output_dir cannot be empty");
    }

    if (config.hooks.empty()) {
        errors_.push_back("No hooks defined");
    }

    for (size_t i = 0; i < config.hooks.size(); ++i) {
        const auto& hook = config.hooks[i];
        std::string prefix = "Hook[" + std::to_string(i) + "]: ";

        if (hook.target.moduleName.empty()) {
            errors_.push_back(prefix + "module name is empty");
        }

        if (!hook.target.useRva && hook.target.symbolName.empty()) {
            errors_.push_back(prefix + "symbol name is empty (and not using RVA)");
        }

        if (hook.action == HookAction::Replace || hook.action == HookAction::CaptureAndReplace) {
            if (hook.replacerModule.empty() && hook.replacerSymbol.empty()) {
                errors_.push_back(prefix + "replace action requires replace_module and replace_symbol");
            }
        }

        if (hook.collectorType == CollectorType::Custom && hook.customCollectorName.empty()) {
            errors_.push_back(prefix + "custom collector requires custom_collector_name");
        }
    }

    for (const auto& err : errors_) {
        LOG_ERROR("Config validation: {}", err);
    }

    return errors_.empty();
}

}
