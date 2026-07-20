#include "api/hook_api.h"
#include "engine/hook_engine.h"
#include "config/config_loader.h"
#include "config/config_validator.h"
#include "collector/collector_factory.h"
#include "replacer/dll_replacer.h"
#include "replacer/callback_replacer.h"
#include "output/trace_output_writer.h"
#include "output/perfetto_tracer.h"
#include "output/hook_data_source.h"
#include "logging/logger.h"
#include <Windows.h>
#include <filesystem>
#include <format>

namespace hookfw {

struct HookBuilder::Impl {
    HookEngine* engine = nullptr;
    HookDescriptor desc;
    PreHookCallback preCb;
    PostHookCallback postCb;
    void* replacePtr = nullptr;
};

HookBuilder::HookBuilder(HookEngine* engine, const HookTarget& target)
    : impl_(std::make_unique<Impl>()) {
    impl_->engine = engine;
    impl_->desc.target = target;
}

HookBuilder::~HookBuilder() = default;

HookBuilder& HookBuilder::params(std::initializer_list<ParamDescriptor> paramList) {
    impl_->desc.params = paramList;
    if (impl_->desc.collectorType == CollectorType::Raw) {
        impl_->desc.collectorType = CollectorType::Typed;
    }
    return *this;
}

HookBuilder& HookBuilder::returnType(ParamType type) {
    impl_->desc.returnType = type;
    return *this;
}

HookBuilder& HookBuilder::collector(CollectorType type) {
    impl_->desc.collectorType = type;
    return *this;
}

HookBuilder& HookBuilder::onEntry(PreHookCallback cb) {
    impl_->preCb = std::move(cb);
    return *this;
}

HookBuilder& HookBuilder::onExit(PostHookCallback cb) {
    impl_->postCb = std::move(cb);
    return *this;
}

HookBuilder& HookBuilder::replace(void* replacementFunc) {
    impl_->replacePtr = replacementFunc;
    impl_->desc.action = HookAction::Replace;
    return *this;
}

HookBuilder& HookBuilder::replaceFromDll(const std::string& dllPath, const std::string& funcName) {
    impl_->desc.replacerModule = dllPath;
    impl_->desc.replacerSymbol = funcName;
    impl_->desc.action = HookAction::Replace;
    return *this;
}

HookBuilder& HookBuilder::injector(const std::string& name) {
    impl_->desc.injectorName = name;
    return *this;
}

HookBuilder& HookBuilder::injectorMode(InjectorMode mode) {
    impl_->desc.injectorMode = mode;
    return *this;
}

bool HookBuilder::install() {
    try {
        auto collector = CollectorFactory::create(impl_->desc);
        if (!collector) {
            LOG_ERROR("Failed to create collector");
            return false;
        }

        auto writer = HookEngineCore::instance().getOutputWriter();
        if (writer) {
            collector->setOutputWriter(writer);
        }

        std::string funcName = impl_->desc.target.useRva
            ? std::format("RVA_0x{:08x}", impl_->desc.target.rva)
            : impl_->desc.target.symbolName;

        collector->setModuleName(impl_->desc.target.moduleName);
        collector->setFunctionName(funcName);

        std::shared_ptr<ReplacerBase> replacer;
        if (impl_->replacePtr) {
            replacer = std::make_shared<CallbackReplacer>(impl_->replacePtr);
        } else if (!impl_->desc.replacerModule.empty()) {
            replacer = std::make_shared<DllReplacer>(
                impl_->desc.replacerModule, impl_->desc.replacerSymbol);
        }

        return HookEngineCore::instance().addHook(impl_->desc, collector, replacer) != 0;
    } catch (const std::exception& ex) {
        LOG_ERROR("HookBuilder::install exception: {}", ex.what());
        return false;
    }
}

// --- HookEngine ---

struct HookEngine::Impl {
    std::string outputDir = "dump_data";
    std::string dllDir;
    bool initialized = false;
};

HookEngine::HookEngine() : impl_(std::make_unique<Impl>()) {}
HookEngine::~HookEngine() { shutdown(); }

HookEngine& HookEngine::instance() {
    static HookEngine inst;
    return inst;
}

bool HookEngine::initialize(const std::string& configPath) {
    try {
        if (impl_->initialized) {
            LOG_WARN("HookEngine already initialized");
            return true;
        }

        LOG_INFO("HookEngine initializing...");

        std::shared_ptr<OutputWriter> writer = std::make_shared<TraceOutputWriter>(impl_->outputDir);
        HookEngineCore::instance().setOutputWriter(writer);

        // Ensure all custom collectors are registered
        registerAllCollectors();

        PerfettoTracer::instance().setOutputDir(impl_->outputDir);

        if (!configPath.empty()) {
            ConfigLoader loader;
            auto configOpt = loader.load(configPath);
            if (!configOpt) {
                LOG_ERROR("Failed to load config: {}", configPath);
                return false;
            }

            ConfigValidator validator;
            if (!validator.validate(*configOpt)) {
                LOG_ERROR("Config validation failed");
                return false;
            }

            impl_->outputDir = configOpt->outputDir;
            if (!impl_->dllDir.empty() && std::filesystem::path(impl_->outputDir).is_relative()) {
                impl_->outputDir = (std::filesystem::path(impl_->dllDir) / impl_->outputDir).string();
            }
            writer = std::make_shared<TraceOutputWriter>(impl_->outputDir);
            LOG_INFO("Output format: Perfetto");
            HookEngineCore::instance().setOutputWriter(writer);

            int traceCount = 0, captureCount = 0;
            for (const auto& hookDesc : configOpt->hooks) {
                std::string funcName;
                if (!hookDesc.displayName.empty()) {
                    funcName = hookDesc.displayName;
                } else if (hookDesc.target.useRva) {
                    funcName = std::format("RVA_0x{:08x}", hookDesc.target.rva);
                } else {
                    funcName = hookDesc.target.symbolName;
                }

                std::shared_ptr<CollectorBase> collector;
                std::shared_ptr<ReplacerBase> replacer;

                if (hookDesc.mode == HookMode::Capture) {
                    collector = CollectorFactory::create(hookDesc);
                    if (collector) {
                        collector->setOutputWriter(writer);
                        collector->setModuleName(hookDesc.target.moduleName);
                        collector->setFunctionName(funcName);
                    }
                    if (!hookDesc.replacerModule.empty()) {
                        replacer = std::make_shared<DllReplacer>(
                            hookDesc.replacerModule, hookDesc.replacerSymbol);
                    }
                }

                uint32_t hookId = HookEngineCore::instance().addHook(hookDesc, collector, replacer);
                if (hookId == 0) {
                    LOG_ERROR("Failed to add hook: {}!{}",
                              hookDesc.target.moduleName, funcName);
                    continue;
                }

                auto* ih = HookEngineCore::instance().getHookById(hookId);
                uint64_t rva = 0;
                if (ih && ih->moduleBase > 0) {
                    rva = reinterpret_cast<uintptr_t>(ih->resolvedAddress) - ih->moduleBase;
                } else if (hookDesc.target.useRva) {
                    rva = hookDesc.target.rva;
                }

                if (hookDesc.mode == HookMode::Trace) {
                    std::string displayName = TraceOutputWriter::undecorateName(funcName);
                    if (rva > 0) {
                        displayName += std::format(" @RVA_0x{:08x}", rva);
                    }
                    HookDataSource::registerHookName(hookId, displayName);
                    traceCount++;
                } else {
                    if (collector && rva > 0) {
                        std::string demangled = TraceOutputWriter::undecorateName(funcName);
                        std::string nameWithRva = demangled + std::format(" @RVA_0x{:08x}", rva);
                        collector->setFunctionName(nameWithRva);
                    }
                    captureCount++;
                }
            }
            LOG_INFO("Hooks registered: {} trace, {} capture", traceCount, captureCount);

            PerfettoTracer::instance().setOutputDir(impl_->outputDir);
        }

        impl_->initialized = true;
        LOG_INFO("HookEngine initialized successfully");
        return true;
    } catch (const std::exception& ex) {
        LOG_CRITICAL("HookEngine::initialize exception: {}", ex.what());
        return false;
    }
}

void HookEngine::shutdown() {
    try {
        if (!impl_->initialized) return;
        LOG_INFO("HookEngine shutting down...");
        PerfettoTracer::instance().shutdown();
        HookEngineCore::instance().uninstallAll();

        auto writer = HookEngineCore::instance().getOutputWriter();
        if (writer) {
            writer->finalize();
        }

        impl_->initialized = false;
        Logger::instance().shutdown();
    } catch (const std::exception& ex) {
        OutputDebugStringA(("HookEngine::shutdown exception: " + std::string(ex.what()) + "\n").c_str());
    }
}

HookBuilder HookEngine::hook(const std::string& moduleName, const std::string& symbolName) {
    HookTarget target;
    target.moduleName = moduleName;
    target.symbolName = symbolName;
    target.useRva = false;
    return HookBuilder(this, target);
}

HookBuilder HookEngine::hookByRva(const std::string& moduleName, uint64_t rva) {
    HookTarget target;
    target.moduleName = moduleName;
    target.rva = rva;
    target.useRva = true;
    return HookBuilder(this, target);
}

bool HookEngine::installAll() {
    return HookEngineCore::instance().installAll();
}

void HookEngine::uninstallAll() {
    HookEngineCore::instance().uninstallAll();
}

void HookEngine::setOutputDirectory(const std::string& path) {
    impl_->outputDir = path;
    auto writer = std::make_shared<TraceOutputWriter>(path);
    HookEngineCore::instance().setOutputWriter(writer);
}

void HookEngine::setDllDirectory(const std::string& path) {
    impl_->dllDir = path;
}

// --- Global functions ---

void initializeFramework(const std::string& configPath) {
    HookEngine::instance().initialize(configPath);
    if (!HookEngine::instance().installAll()) {
        LOG_CRITICAL("Hook installation FAILED - no hooks will be active!");
    }
}

void shutdownFramework() {
    HookEngine::instance().shutdown();
}

}
