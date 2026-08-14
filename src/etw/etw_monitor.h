#pragma once

#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace hookfw {

// Invoked for every Image Load event (Load only) delivered by the session.
// processId: id of the process that loaded the image.
// imageBase: base address the image was loaded at.
// filePath:  full path of the loaded DLL/EXE image.
using ImageLoadCallback =
    std::function<void(ULONG processId, ULONG_PTR imageBase, const std::wstring& filePath)>;


// Performance-friendly real-time ETW monitor for kernel Image Load events.
//
// Design notes:
//  - Enables EVENT_TRACE_FLAG_IMAGE_LOAD (system-wide). PID/EXECUTABLE_NAME
//    provider filters are NOT used because they do not apply to kernel Image
//    Load events; callers filter in the callback.
//  - Parses the fixed Image_Load MOF layout directly from the event's UserData
//    (no TDH lookups, no allocations in the hot path).
//  - The callback is invoked on a single consumer thread.
//  - Requires the process to run elevated (admin / Performance Log Users).
class EtwImageLoadMonitor {
public:
    EtwImageLoadMonitor() = default;
    ~EtwImageLoadMonitor();

    EtwImageLoadMonitor(const EtwImageLoadMonitor&) = delete;
    EtwImageLoadMonitor& operator=(const EtwImageLoadMonitor&) = delete;

    // Session name used by StartTraceW. Optional; a default is chosen if unset.
    void setSessionName(const std::wstring& name);

    // Starts the session. Returns ERROR_SUCCESS on success, otherwise a Win32
    // error code.
    ULONG start(ImageLoadCallback imageCb = {});

    // Stops the session and joins the consumer thread. Safe to call multiple
    // times and from the destructor.
    void stop();

private:
    ULONG startInternal();
    static void WINAPI eventRecordCallback(PEVENT_RECORD eventRecord);

    TRACEHANDLE session_ = 0;   // controller handle (StartTraceW)
    TRACEHANDLE trace_ = 0;     // consumer handle (OpenTraceW)
    std::wstring sessionName_;
    ImageLoadCallback imageCallback_;
    std::thread consumerThread_;
    std::atomic<bool> running_{false};
};

}  // namespace hookfw
