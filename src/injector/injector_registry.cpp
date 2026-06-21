#include "hook_framework/injector_base.h"
#include "logging/logger.h"

namespace hookfw {

InjectorRegistry& InjectorRegistry::instance() {
    static InjectorRegistry inst;
    return inst;
}

void InjectorRegistry::registerCreator(const std::string& name, InjectorCreator creator) {
    std::lock_guard lock(mutex_);
    creators_[name] = std::move(creator);
    LOG_INFO("Injector registered: {}", name);
}

std::shared_ptr<InjectorBase> InjectorRegistry::create(const std::string& name) {
    std::lock_guard lock(mutex_);
    auto it = creators_.find(name);
    if (it == creators_.end()) {
        LOG_WARN("Injector not found: {}", name);
        return nullptr;
    }
    return it->second();
}

}
