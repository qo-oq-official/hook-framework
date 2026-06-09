#pragma once
#include <Windows.h>
#include <detours.h>
#include "logging/logger.h"

namespace hookfw {

class HookTransaction {
public:
    HookTransaction() : committed_(false), valid_(false) {
        LONG result = DetourTransactionBegin();
        if (result != NO_ERROR) {
            LOG_ERROR("DetourTransactionBegin failed, error={}", result);
            return;
        }
        result = DetourUpdateThread(GetCurrentThread());
        if (result != NO_ERROR) {
            LOG_ERROR("DetourUpdateThread failed, error={}", result);
            DetourTransactionAbort();
            return;
        }
        valid_ = true;
        LOG_TRACE("Hook transaction started");
    }

    ~HookTransaction() {
        if (valid_ && !committed_) {
            LONG result = DetourTransactionAbort();
            LOG_WARN("Hook transaction aborted, result={}", result);
        }
    }

    bool isValid() const { return valid_; }

    bool attach(void** ppPointer, void* pDetour) {
        if (!valid_) {
            LOG_ERROR("Cannot attach: transaction invalid");
            return false;
        }
        LONG result = DetourAttach(ppPointer, pDetour);
        if (result != NO_ERROR) {
            LOG_ERROR("DetourAttach failed, error={}", result);
            return false;
        }
        LOG_TRACE("DetourAttach success, target={:#x}, detour={:#x}",
                  reinterpret_cast<uintptr_t>(*ppPointer),
                  reinterpret_cast<uintptr_t>(pDetour));
        return true;
    }

    bool detach(void** ppPointer, void* pDetour) {
        if (!valid_) {
            LOG_ERROR("Cannot detach: transaction invalid");
            return false;
        }
        LONG result = DetourDetach(ppPointer, pDetour);
        if (result != NO_ERROR) {
            LOG_ERROR("DetourDetach failed, error={}", result);
            return false;
        }
        return true;
    }

    bool commit() {
        if (!valid_) {
            LOG_ERROR("Cannot commit: transaction invalid");
            return false;
        }
        LONG result = DetourTransactionCommit();
        if (result != NO_ERROR) {
            LOG_ERROR("DetourTransactionCommit failed, error={}", result);
            return false;
        }
        committed_ = true;
        LOG_INFO("Hook transaction committed successfully");
        return true;
    }

    HookTransaction(const HookTransaction&) = delete;
    HookTransaction& operator=(const HookTransaction&) = delete;

private:
    bool valid_;
    bool committed_;
};

}
