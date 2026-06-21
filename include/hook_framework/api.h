#pragma once
#include "types.h"
#include <functional>
#include <memory>

#ifdef HOOKFW_EXPORTS
#define HOOKFW_API __declspec(dllexport)
#else
#define HOOKFW_API __declspec(dllimport)
#endif

namespace hookfw {

struct HookContext;
class HookEngine;

using PreHookCallback = std::function<void(const HookContext&)>;
using PostHookCallback = std::function<void(const HookContext&)>;

class HOOKFW_API HookBuilder {
public:
    HookBuilder(HookEngine* engine, const HookTarget& target);
    ~HookBuilder();

    HookBuilder& params(std::initializer_list<ParamDescriptor> paramList);
    HookBuilder& returnType(ParamType type);
    HookBuilder& collector(CollectorType type);
    HookBuilder& onEntry(PreHookCallback cb);
    HookBuilder& onExit(PostHookCallback cb);
    HookBuilder& injector(const std::string& name);
    HookBuilder& injectorMode(InjectorMode mode);
    HookBuilder& replace(void* replacementFunc);
    HookBuilder& replaceFromDll(const std::string& dllPath, const std::string& funcName);
    bool install();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class HOOKFW_API HookEngine {
public:
    static HookEngine& instance();

    bool initialize(const std::string& configPath = "");
    void shutdown();

    HookBuilder hook(const std::string& moduleName, const std::string& symbolName);
    HookBuilder hookByRva(const std::string& moduleName, uint64_t rva);

    bool installAll();
    void uninstallAll();

    void setOutputDirectory(const std::string& path);
    void setDllDirectory(const std::string& path);

private:
    HookEngine();
    ~HookEngine();
    HookEngine(const HookEngine&) = delete;
    HookEngine& operator=(const HookEngine&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
