#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace hookfw {

// COM interface method descriptor
struct ComMethodDesc {
    std::string name;
    int vtableIndex;
    std::vector<std::string> paramNames;
    std::vector<std::string> paramTypes;  // hooks.json type strings
    std::string returnType;
};

// COM interface metadata
struct ComInterfaceDesc {
    std::string iid;
    std::string name;
    std::vector<ComMethodDesc> methods;
};

class ComRegistry {
public:
    static ComRegistry& instance();
    void registerInterface(const ComInterfaceDesc& iface);
    const ComInterfaceDesc* findInterface(const std::string& iid) const;
    bool isTargetIID(const std::string& iid) const;
    size_t interfaceCount() const;

private:
    ComRegistry() = default;
    std::unordered_map<std::string, ComInterfaceDesc> interfaces_;
    mutable std::mutex mutex_;
};

}  // namespace hookfw
