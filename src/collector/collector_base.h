#pragma once
#include "thunk/hook_context.h"
#include "hook_framework/types.h"
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>

namespace hookfw {

class CollectorBase {
public:
    virtual ~CollectorBase() = default;

    virtual void onEntry(const HookContext& ctx) = 0;
    virtual void onExit(const HookContext& ctx) = 0;

    void setModuleName(const std::string& name) { moduleName_ = name; }
    void setFunctionName(const std::string& name) { functionName_ = name; }
    void setModuleBase(uintptr_t base) { moduleBase_ = base; }

protected:
    std::string moduleName_;
    std::string functionName_;
    uintptr_t moduleBase_ = 0;
};

using CollectorCreator = std::function<std::shared_ptr<CollectorBase>()>;

class CollectorRegistry {
public:
    static CollectorRegistry& instance();

    void registerCreator(const std::string& name, CollectorCreator creator);
    std::shared_ptr<CollectorBase> create(const std::string& name);

private:
    CollectorRegistry() = default;
    std::unordered_map<std::string, CollectorCreator> creators_;
};

void registerAllCollectors();

}

#define REGISTER_COLLECTOR(name, className) \
    namespace { \
    static bool _reg_##className = []() { \
        ::hookfw::CollectorRegistry::instance().registerCreator(name, \
            []() -> std::shared_ptr<::hookfw::CollectorBase> { \
                return std::make_shared<className>(); \
            }); \
        return true; \
    }(); \
    }
