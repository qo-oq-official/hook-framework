#include "com/com_registry.h"

namespace hookfw {

ComRegistry& ComRegistry::instance() {
    static ComRegistry inst;
    return inst;
}

void ComRegistry::registerInterface(const ComInterfaceDesc& iface) {
    std::lock_guard lock(mutex_);
    interfaces_[iface.iid] = iface;
}

const ComInterfaceDesc* ComRegistry::findInterface(const std::string& iid) const {
    std::lock_guard lock(mutex_);
    auto it = interfaces_.find(iid);
    return (it != interfaces_.end()) ? &it->second : nullptr;
}

bool ComRegistry::isTargetIID(const std::string& iid) const {
    std::lock_guard lock(mutex_);
    return interfaces_.find(iid) != interfaces_.end();
}

size_t ComRegistry::interfaceCount() const {
    std::lock_guard lock(mutex_);
    return interfaces_.size();
}

}  // namespace hookfw
