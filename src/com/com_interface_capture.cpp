#include "com/com_interface_capture.h"
#include "com/com_registry.h"
#include "com/com_hook_engine.h"
#include "logging/logger.h"
#include <nlohmann/json.hpp>
#include <format>

namespace hookfw {

void ComInterfaceCaptureCollector::onEntry(const HookContext& ctx) {
    // CoCreateInstance: args are rclsid(0), pUnkOuter(1), dwClsContext(2), riid(3), ppv(4)
    // QueryInterface: args are this(0), riid(1), ppv(2)
    if (!outputWriter_) return;

    auto* riid = ctx.arg<void*>(1);  // riid is typically arg1 for QI
    if (!riid) riid = ctx.arg<void*>(3);  // or arg3 for CoCreateInstance
    
    if (riid) {
        // IID is 16 bytes. Read as GUID string for logging.
        auto* guid = reinterpret_cast<unsigned int*>(riid);
        char buf[64];
        snprintf(buf, sizeof(buf), "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                 guid[0], (guid[1] >> 16) & 0xFFFF, guid[1] & 0xFFFF,
                 ((unsigned char*)&guid[2])[0], ((unsigned char*)&guid[2])[1],
                 ((unsigned char*)&guid[2])[2], ((unsigned char*)&guid[2])[3],
                 ((unsigned char*)&guid[2])[4], ((unsigned char*)&guid[2])[5],
                 ((unsigned char*)&guid[2])[6], ((unsigned char*)&guid[2])[7]);

        if (ComRegistry::instance().isTargetIID(buf)) {
            LOG_INFO("ComCapture: target IID {} detected", buf);
        }
    }

    nlohmann::json j;
    j["hook_id"] = ctx.hookId;
    j["phase"] = "entry";
    j["function"] = functionName_;
    outputWriter_->writeJson(moduleName_, functionName_, 0, "entry", j);
}

void ComInterfaceCaptureCollector::onExit(const HookContext& ctx) {
    if (!outputWriter_) return;

    // Read IID
    auto* riid = ctx.arg<void*>(1);  // QueryInterface: riid at index 1
    if (!riid) riid = ctx.arg<void*>(3);  // CoCreateInstance: riid at index 3

    // Read ppv (interface pointer)
    void* ppv = nullptr;
    void** ppvPtr = ctx.arg<void**>(2);  // QI: ppv at index 2
    if (!ppvPtr) ppvPtr = ctx.arg<void**>(4);  // CoCreateInstance: ppv at index 4
    if (ppvPtr && !IsBadReadPtr(ppvPtr, sizeof(void*)))
        ppv = *ppvPtr;

    std::string iidStr;
    if (riid && !IsBadReadPtr(riid, 16)) {
        auto* guid = reinterpret_cast<unsigned int*>(riid);
        char buf[64];
        snprintf(buf, sizeof(buf), "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                 guid[0], (guid[1] >> 16) & 0xFFFF, guid[1] & 0xFFFF,
                 ((unsigned char*)&guid[2])[0], ((unsigned char*)&guid[2])[1],
                 ((unsigned char*)&guid[2])[2], ((unsigned char*)&guid[2])[3],
                 ((unsigned char*)&guid[2])[4], ((unsigned char*)&guid[2])[5],
                 ((unsigned char*)&guid[2])[6], ((unsigned char*)&guid[2])[7]);
        iidStr = buf;
    }

    nlohmann::json j;
    j["hook_id"] = ctx.hookId;
    j["phase"] = "exit";
    j["function"] = functionName_;
    j["iid"] = iidStr;
    j["ppv"] = ppv ? std::format("0x{:016x}", reinterpret_cast<uint64_t>(ppv)) : "null";

    if (ppv && ComRegistry::instance().isTargetIID(iidStr)) {
        int patched = ComHookEngine::instance().patchInterface(ppv, iidStr);
        j["vtable_patched"] = patched;
        LOG_INFO("ComCapture: patched {} vtable slots on {:p} for {}",
                 patched, ppv, iidStr);
    }

    outputWriter_->writeJson(moduleName_, functionName_, 0, "exit", j);
}

REGISTER_COLLECTOR("com_interface_capture", ComInterfaceCaptureCollector);

}  // namespace hookfw
