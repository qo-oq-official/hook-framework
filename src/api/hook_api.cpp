#include "api/hook_api.h"
#include "engine/hook_engine.h"
#include "config/config_loader.h"
#include "config/config_validator.h"
#include "collector/collector_factory.h"
#include "replacer/dll_replacer.h"
#include "replacer/callback_replacer.h"
#include "output/file_output_writer.h"
#include "output/trace_output_writer.h"
#include "logging/logger.h"
#include <Windows.h>
#include <filesystem>
#include <format>
#include <cstring>

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
            ? std::format("RVA_{:#x}", impl_->desc.target.rva)
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

        return HookEngineCore::instance().addHook(impl_->desc, collector, replacer);
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

        Logger::instance().initialize();
        LOG_INFO("HookEngine initializing...");

        std::shared_ptr<OutputWriter> writer = std::make_shared<FileOutputWriter>(impl_->outputDir);
        HookEngineCore::instance().setOutputWriter(writer);

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
            if (configOpt->outputFormat == OutputFormat::Perfetto) {
                writer = std::make_shared<TraceOutputWriter>(impl_->outputDir);
                LOG_INFO("Output format: Perfetto (trace.json)");
            } else {
                writer = std::make_shared<FileOutputWriter>(impl_->outputDir);
                LOG_INFO("Output format: File (per-call files)");
            }
            HookEngineCore::instance().setOutputWriter(writer);

            for (const auto& hookDesc : configOpt->hooks) {
                auto collector = CollectorFactory::create(hookDesc);
                if (collector) {
                    collector->setOutputWriter(writer);
                    collector->setModuleName(hookDesc.target.moduleName);

                    std::string funcName = hookDesc.target.useRva
                        ? std::format("RVA_{:#x}", hookDesc.target.rva)
                        : hookDesc.target.symbolName;
                    collector->setFunctionName(funcName);
                }

                std::shared_ptr<ReplacerBase> replacer;
                if (!hookDesc.replacerModule.empty()) {
                    replacer = std::make_shared<DllReplacer>(
                        hookDesc.replacerModule, hookDesc.replacerSymbol);
                }

                if (!HookEngineCore::instance().addHook(hookDesc, collector, replacer)) {
                    std::string targetName = hookDesc.target.useRva
                        ? std::format("RVA_{:#x}", hookDesc.target.rva)
                        : hookDesc.target.symbolName;
                    LOG_ERROR("Failed to add hook from config: {}!{}",
                              hookDesc.target.moduleName, targetName);
                }
            }
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
        HookEngineCore::instance().uninstallAll();
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
    auto writer = std::make_shared<FileOutputWriter>(path);
    HookEngineCore::instance().setOutputWriter(writer);
}

void HookEngine::setDllDirectory(const std::string& path) {
    impl_->dllDir = path;
}

// --- Global functions ---

void initializeFramework(const std::string& configPath) {
    HookEngine::instance().initialize(configPath);
    HookEngine::instance().installAll();
}

void shutdownFramework() {
    HookEngine::instance().shutdown();
}

}

// --- ASM dispatch (called from thunk_x64.asm) ---

extern "C" {

void hookfw_dispatch(hookfw::ThunkData* data,
                     uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                     uint64_t origRSP, uint64_t phase, void* xmmPtr) {
    try {
        auto* hook = hookfw::HookEngineCore::instance().getHookById(data->hookId);
        if (!hook || !hook->collector) {
            LOG_ERROR("hookfw_dispatch: hook or collector null, id={}", data->hookId);
            return;
        }

        hookfw::HookContext ctx{};
        ctx.hookId = data->hookId;

        if (phase == 0) {
            ctx.rcx = a0;
            ctx.rdx = a1;
            ctx.r8 = a2;
            ctx.r9 = a3;
            ctx.rsp = origRSP;
            if (xmmPtr) {
                memcpy(ctx.xmm0, xmmPtr, 16);
                memcpy(ctx.xmm1, static_cast<uint8_t*>(xmmPtr) + 16, 16);
                memcpy(ctx.xmm2, static_cast<uint8_t*>(xmmPtr) + 32, 16);
                memcpy(ctx.xmm3, static_cast<uint8_t*>(xmmPtr) + 48, 16);
            }
            if (origRSP) {
                ctx.returnAddress = *reinterpret_cast<uint64_t*>(origRSP);
                size_t copySize = 512;
                if (!IsBadReadPtr(reinterpret_cast<void*>(origRSP), copySize)) {
                    memcpy(ctx.stackSnapshot, reinterpret_cast<void*>(origRSP), copySize);
                    ctx.stackSnapshotSize = 512;
                }
            }
            hook->collector->onEntry(ctx);
        } else {
            ctx.rax = a0;
            if (xmmPtr) {
                memcpy(ctx.xmm0, xmmPtr, 16);
            }
            hook->collector->onExit(ctx);
        }
    } catch (const std::exception& ex) {
        LOG_CRITICAL("hookfw_dispatch exception hook_id={}: {}", data->hookId, ex.what());
    } catch (...) {
        LOG_CRITICAL("hookfw_dispatch unknown exception hook_id={}", data->hookId);
    }
}

}
