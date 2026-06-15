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
        auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(hMod);
        auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            reinterpret_cast<const uint8_t*>(hMod) + dosHeader->e_lfanew);
        DWORD imageSize = ntHeaders->OptionalHeader.SizeOfImage;

        if (target.rva >= imageSize) {
            LOG_ERROR("RVA {:#x} exceeds module '{}' image size ({:#x}). "
                      "This address is likely an absolute VA, not an RVA. "
                      "Correct RVA = absolute_address - module_base",
                      target.rva, target.moduleName, imageSize);
            return nullptr;
        }

        addr = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(hMod) + target.rva);

        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(addr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {
            LOG_ERROR("RVA {:#x} in '{}' resolves to inaccessible memory at {:#x}",
                      target.rva, target.moduleName, reinterpret_cast<uintptr_t>(addr));
            return nullptr;
        }

        LOG_INFO("Resolved {}+{:#x} -> {:#x} (image size={:#x})",
                 target.moduleName, target.rva,
                 reinterpret_cast<uintptr_t>(addr), imageSize);
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

    for (const auto& existing : hooks_) {
        if (existing->resolvedAddress == hook->resolvedAddress) {
            std::string targetName = desc.target.useRva
                ? std::format("RVA_{:#x}", desc.target.rva) : desc.target.symbolName;
            LOG_WARN("Skipping duplicate address {:#x} for {}!{} (already hooked by id={})",
                     reinterpret_cast<uintptr_t>(hook->resolvedAddress),
                     desc.target.moduleName, targetName, existing->id);
            return true;
        }
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

static bool safeDetourAttach(HookTransaction* txn, void** ppOriginal, void* detourTarget,
                             uint32_t hookId, void* resolvedAddr) {
    __try {
        if (!txn->attach(ppOriginal, detourTarget)) {
            return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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

        bool attachOk = safeDetourAttach(&txn, ppOriginal, detourTarget,
                                         hook->id, hook->resolvedAddress);
        if (!attachOk) {
            LOG_ERROR("Failed to attach hook id={} (target={:#x}), skipping",
                      hook->id, reinterpret_cast<uintptr_t>(hook->resolvedAddress));
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

    if (outputWriter_) {
        outputWriter_->flush();
    }

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
