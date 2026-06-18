#include "api/hook_api.h"
#include "engine/hook_engine.h"
#include "config/config_loader.h"
#include "config/config_validator.h"
#include "collector/collector_factory.h"
#include "replacer/dll_replacer.h"
#include "replacer/callback_replacer.h"
#include "output/file_output_writer.h"
#include "output/trace_output_writer.h"
#include "output/trace_flusher.h"
#include "output/ring_buffer.h"
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

            int traceCount = 0, captureCount = 0;
            for (const auto& hookDesc : configOpt->hooks) {
                std::string funcName = hookDesc.target.useRva
                    ? std::format("RVA_{:#x}", hookDesc.target.rva)
                    : hookDesc.target.symbolName;

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

                if (hookDesc.mode == HookMode::Trace) {
                    uint64_t rva = hookDesc.target.useRva ? hookDesc.target.rva : 0;
                    TraceFlusher::instance().registerHookName(
                        hookId, hookDesc.target.moduleName, funcName, rva);
                    traceCount++;
                } else {
                    captureCount++;
                }
            }
            LOG_INFO("Hooks registered: {} trace, {} capture", traceCount, captureCount);

            if (configOpt->outputFormat == OutputFormat::Perfetto) {
                auto traceWriter = std::dynamic_pointer_cast<TraceOutputWriter>(writer);
                if (traceWriter) {
                    TraceFlusher::instance().start(traceWriter);
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
        TraceFlusher::instance().stop();
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
    auto writer = std::make_shared<FileOutputWriter>(path);
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

// --- ASM dispatch (called from thunk_x64.asm) ---

static void crashLog(const char* msg) {
    static HANDLE hFile = INVALID_HANDLE_VALUE;
    if (hFile == INVALID_HANDLE_VALUE) {
        char dllPath[MAX_PATH] = {};
        HMODULE hSelf = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&crashLog), &hSelf);
        GetModuleFileNameA(hSelf, dllPath, MAX_PATH);
        std::string logPath(dllPath);
        auto pos = logPath.rfind('\\');
        if (pos != std::string::npos) logPath = logPath.substr(0, pos + 1);
        logPath += "hook_crash.log";
        hFile = CreateFileA(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            SetFilePointer(hFile, 0, nullptr, FILE_END);
        }
    }
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hFile, msg, static_cast<DWORD>(strlen(msg)), &written, nullptr);
        FlushFileBuffers(hFile);
    }
}

static thread_local bool g_inHookDispatch = false;
static thread_local int g_hookDepth = 0;
static volatile uint32_t g_lastHookId = 0;
static volatile uint64_t g_lastPhase = 0;
static volatile uint64_t g_dispatchCount = 0;
static const int MAX_HOOK_DEPTH = 64;

static void doDispatchBody(hookfw::ThunkData* data,
                           uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                           uint64_t origRSP, uint64_t phase, void* xmmPtr) {
    auto* hook = hookfw::HookEngineCore::instance().getHookById(data->hookId);
    if (!hook || !hook->collector) return;

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
        if (origRSP && !IsBadReadPtr(reinterpret_cast<void*>(origRSP), 512)) {
            ctx.returnAddress = *reinterpret_cast<uint64_t*>(origRSP);
            memcpy(ctx.stackSnapshot, reinterpret_cast<void*>(origRSP), 512);
            ctx.stackSnapshotSize = 512;
        }
        hook->collector->onEntry(ctx);
    } else {
        ctx.rax = a0;
        ctx.rcx = a1;
        ctx.rdx = a2;
        ctx.r8 = a3;
        ctx.r9 = origRSP;
        if (xmmPtr) {
            memcpy(ctx.xmm0, xmmPtr, 16);
        }
        hook->collector->onExit(ctx);
    }
}

extern "C" {

static volatile uint64_t g_quickTraceCallCount = 0;
static volatile bool g_quickTraceFirstLogged = false;
static const uint32_t FLUSH_INTERVAL = 50000;

void hookfw_quick_trace(hookfw::ThunkData* data,
                        uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    if (!data) return;

    static volatile uint64_t diagCount = 0;
    diagCount++;
    if (diagCount <= 5 || diagCount % 50000 == 0) {
        char buf[128];
        wsprintfA(buf, "[hookfw] trace call #%llu hook_id=%u\n", diagCount, data->hookId);
        crashLog(buf);
    }

    g_quickTraceCallCount++;
    if (!g_quickTraceFirstLogged) {
        g_quickTraceFirstLogged = true;
        char buf[128];
        wsprintfA(buf, "[hookfw] quick_trace FIRST CALL hook_id=%u\n", data->hookId);
        crashLog(buf);
    }

    hookfw::TraceRecord rec{};
    rec.hookId = data->hookId;
    rec.threadId = GetCurrentThreadId();
    rec.timestampUs = hookfw::RingBuffer::nowMicroseconds();
    rec.arg0 = a0;
    rec.arg1 = a1;
    rec.arg2 = a2;
    rec.arg3 = a3;
    rec.phase = 0;
    hookfw::TraceFlusher::instance().ringBuffer().push(rec);

    static thread_local uint32_t t_counter = 0;
    if (++t_counter >= FLUSH_INTERVAL) {
        t_counter = 0;
        hookfw::TraceFlusher::instance().syncFlush();
    }
}

void hookfw_quick_trace_exit(hookfw::ThunkData* data,
                             uint64_t retval, uint64_t, uint64_t, uint64_t) {
    if (!data) return;
    hookfw::TraceRecord rec{};
    rec.hookId = data->hookId;
    rec.threadId = GetCurrentThreadId();
    rec.timestampUs = hookfw::RingBuffer::nowMicroseconds();
    rec.arg0 = retval;
    rec.phase = 1;
    hookfw::TraceFlusher::instance().ringBuffer().push(rec);

    static thread_local uint32_t t_counter = 0;
    if (++t_counter >= FLUSH_INTERVAL) {
        t_counter = 0;
        hookfw::TraceFlusher::instance().syncFlush();
    }
}

void hookfw_dispatch(hookfw::ThunkData* data,
                     uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                     uint64_t origRSP, uint64_t phase, void* xmmPtr) {
    if (g_inHookDispatch) return;
    if (!data) return;

    g_lastHookId = data->hookId;
    g_lastPhase = phase;
    g_dispatchCount++;
    g_inHookDispatch = true;

    __try {
        doDispatchBody(data, a0, a1, a2, a3, origRSP, phase, xmmPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        char buf[256];
        wsprintfA(buf, "[CRASH] hook_id=%u phase=%llu count=%llu code=0x%08X\n",
                  g_lastHookId, g_lastPhase, g_dispatchCount, GetExceptionCode());
        crashLog(buf);
    }

    g_inHookDispatch = false;
}

}
