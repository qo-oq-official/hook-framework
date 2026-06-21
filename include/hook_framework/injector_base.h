#pragma once
#include "hook_framework/types.h"
#include <string>
#include <functional>
#include <memory>
#include <unordered_map>
#include <mutex>

namespace hookfw {

struct HookContext;
class TraceOutputWriter;

class InjectorBase {
public:
    virtual ~InjectorBase() = default;
    virtual std::string name() const = 0;
    virtual void onInject(HookContext& ctx, TraceOutputWriter* trace) = 0;
};

using InjectorCreator = std::function<std::shared_ptr<InjectorBase>()>;

class InjectorRegistry {
public:
    static InjectorRegistry& instance();

    void registerCreator(const std::string& name, InjectorCreator creator);
    std::shared_ptr<InjectorBase> create(const std::string& name);

private:
    InjectorRegistry() = default;
    std::unordered_map<std::string, InjectorCreator> creators_;
    std::mutex mutex_;
};

#define REGISTER_INJECTOR(name, className) \
    namespace { \
    static bool _reg_##className = []() { \
        ::hookfw::InjectorRegistry::instance().registerCreator(name, \
            []() -> std::shared_ptr<::hookfw::InjectorBase> { \
                return std::make_shared<className>(); \
            }); \
        return true; \
    }(); \
    }

}
