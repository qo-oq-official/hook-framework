#include "engine/hook_engine.h"
#include "engine/hook_transaction.h"
#include "logging/logger.h"
#include <Windows.h>
#include <filesystem>
#include <format>

namespace hookfw {

HookEngineCore& HookEngineCore::instance() {
    static HookEngineCore inst;
    return inst;
}

HookEngineCore::~HookEngineCore() {
    uninstallAll();
}

void* HookEngineCore::resolveAddress(const HookTarget& target) {
    HMODULE hMod = GetModuleHandleA(target.moduleName.c_str());
    if (!hMod) {
        LOG_WARN("GetModuleHandle failed for '{}', trying LoadLibrary...", target.moduleName);
        hMod = LoadLibraryA(target.moduleName.c_str());
    }
    if (!hMod) {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        auto fullPath = (exeDir / target.moduleName).string();
        LOG_WARN("LoadLibrary failed, trying full path: {}", fullPath);
        hMod = LoadLibraryA(fullPath.c_str());
    }
    if (!hMod) {
        LOG_ERROR("Cannot resolve module: {}, error={}", target.moduleName, GetLastError());
        return nullptr;
    }
    LOG_INFO("Module '{}' resolved at base {:#x}", target.moduleName,
             reinterpret_cast<uintptr_t>(hMod));

    void* addr = nullptr;
    if (target.useRva) {
        addr = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(hMod) + target.rva);
        LOG_INFO("Resolved {}+{:#x} -> {:#x}", target.moduleName, target.rva,
                 reinterpret_cast<uintptr_t>(addr));
    } else {
        addr = reinterpret_cast<void*>(
            GetProcAddress(hMod, target.symbolName.c_str()));
        if (!addr) {
            LOG_ERROR("Cannot resolve symbol: {}!{}, error={}",
                      target.moduleName, target.symbolName, GetLastError());
            return nullptr;
        }
        LOG_INFO("Resolved {}!{} -> {:#x}", target.moduleName, target.symbolName,
                 reinterpret_cast<uintptr_t>(addr));
    }
    return addr;
}

bool HookEngineCore::addHook(const HookDescriptor& desc,
                             std::shared_ptr<CollectorBase> collector,
                             std::shared_ptr<ReplacerBase> replacer) {
    std::lock_guard lock(mutex_);

    auto hook = std::make_unique<InstalledHook>();
    hook->id = nextId_++;
    hook->descriptor = desc;
    hook->collector = std::move(collector);
    hook->replacer = std::move(replacer);

    hook->resolvedAddress = resolveAddress(desc.target);
    if (!hook->resolvedAddress) {
        LOG_ERROR("Failed to resolve hook target: module={}, symbol={}, rva={:#x}",
                  desc.target.moduleName, desc.target.symbolName, desc.target.rva);
        return false;
    }

    void* replacement = nullptr;
    if (hook->replacer) {
        replacement = hook->replacer->getReplacementAddress();
    }

    bool callOriginal = (desc.action != HookAction::Replace);
    hook->thunkSlot = ThunkManager::instance().allocateThunk(
        hook->id, hook->resolvedAddress, replacement, callOriginal);

    if (!hook->thunkSlot) {
        LOG_ERROR("Failed to allocate thunk for hook id={}", hook->id);
        return false;
    }

    std::string targetName = desc.target.useRva
        ? std::format("RVA_{:#x}", desc.target.rva)
        : desc.target.symbolName;

    LOG_INFO("Hook added: id={}, module={}, target={}, action={}",
             hook->id, desc.target.moduleName, targetName,
             static_cast<int>(desc.action));

    hooks_.push_back(std::move(hook));
    return true;
}

bool HookEngineCore::installAll() {
    std::lock_guard lock(mutex_);

    if (installed_) {
        LOG_WARN("Hooks already installed");
        return true;
    }

    if (hooks_.empty()) {
        LOG_WARN("No hooks to install");
        return true;
    }

    HookTransaction txn;
    if (!txn.isValid()) {
        LOG_ERROR("Failed to start hook transaction");
        return false;
    }

    int attached = 0;
    for (auto& hook : hooks_) {
        if (!hook->thunkSlot) continue;

        hook->thunkSlot->data.trampolineFunction = hook->resolvedAddress;
        void** ppOriginal = &hook->thunkSlot->data.trampolineFunction;
        void* detourTarget = hook->thunkSlot->executableStub;

        if (!txn.attach(ppOriginal, detourTarget)) {
            LOG_ERROR("Failed to attach hook id={}, skipping", hook->id);
            hook->thunkSlot = nullptr;
            continue;
        }
        attached++;
    }

    if (attached == 0) {
        LOG_ERROR("No hooks attached successfully");
        return false;
    }

    if (!txn.commit()) {
        LOG_ERROR("Failed to commit hook transaction");
        return false;
    }

    installed_ = true;
    LOG_INFO("{}/{} hooks installed successfully", attached, hooks_.size());
    return true;
}

void HookEngineCore::uninstallAll() {
    std::lock_guard lock(mutex_);

    if (!installed_) return;

    HookTransaction txn;
    if (!txn.isValid()) {
        LOG_ERROR("Failed to start unhook transaction");
        return;
    }

    for (auto& hook : hooks_) {
        if (!hook->thunkSlot) continue;

        void** ppTrampoline = &hook->thunkSlot->data.trampolineFunction;
        void* detourTarget = hook->thunkSlot->executableStub;

        if (!txn.detach(ppTrampoline, detourTarget)) {
            LOG_ERROR("Failed to detach hook id={}", hook->id);
        }
    }

    if (!txn.commit()) {
        LOG_ERROR("Failed to commit unhook transaction");
    }

    ThunkManager::instance().freeAll();
    installed_ = false;
    LOG_INFO("All hooks uninstalled");
}

InstalledHook* HookEngineCore::getHookById(uint32_t id) {
    std::lock_guard lock(mutex_);
    for (auto& hook : hooks_) {
        if (hook->id == id) return hook.get();
    }
    return nullptr;
}

void HookEngineCore::setOutputWriter(std::shared_ptr<OutputWriter> writer) {
    std::lock_guard lock(mutex_);
    outputWriter_ = std::move(writer);
}

std::shared_ptr<OutputWriter> HookEngineCore::getOutputWriter() {
    std::lock_guard lock(mutex_);
    return outputWriter_;
}

}
