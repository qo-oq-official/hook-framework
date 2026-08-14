#define INITGUID
#include "etw/etw_monitor.h"

#include "logging.h"

#include <cstring>
#include <vector>

namespace hookfw {

namespace {

// Fixed layout of the Image_Load MOF class (see MSDN "Image_Load class").
struct ImageLoadEvent {
    ULONG_PTR ImageBase;      // WmiDataId(1), Pointer
    ULONG_PTR ImageSize;      // WmiDataId(2), size_t
    ULONG     ProcessId;      // WmiDataId(3)
    ULONG     ImageChecksum;  // WmiDataId(4)
    ULONG     TimeDateStamp;  // WmiDataId(5)
    ULONG     Reserved0;      // WmiDataId(6)
    ULONG_PTR DefaultBase;    // WmiDataId(7), Pointer
    ULONG     Reserved1;      // WmiDataId(8)
    ULONG     Reserved2;      // WmiDataId(9)
    ULONG     Reserved3;      // WmiDataId(10)
    ULONG     Reserved4;      // WmiDataId(11)
    WCHAR     FileName[1];    // WmiDataId(12), null-terminated WCHAR
};

static_assert(offsetof(ImageLoadEvent, ProcessId) == 2 * sizeof(ULONG_PTR),
              "unexpected Image_Load ProcessId offset");
static_assert(offsetof(ImageLoadEvent, FileName) == 3 * sizeof(ULONG_PTR) + 8 * sizeof(ULONG),
              "unexpected Image_Load FileName offset");

// Kernel provider GUID for Image Load events (not exported by the SDK headers).
const GUID kImageLoadGuid = {0x2cb15d1d, 0x5fc1, 0x11d2, {0xab, 0xe1, 0x00, 0xa0, 0xc9, 0x11, 0xf5, 0x18}};

// Reads a bounded, null-terminated WCHAR string at `offset` within `data`.
bool readWideString(const BYTE* data, ULONG dataLen, ULONG offset, std::wstring& out) {
    if (dataLen < offset + sizeof(WCHAR)) {
        return false;
    }
    const ULONG maxChars = (dataLen - offset) / sizeof(WCHAR);
    const WCHAR* begin = reinterpret_cast<const WCHAR*>(data + offset);
    ULONG len = 0;
    while (len < maxChars && begin[len] != L'\0') {
        ++len;
    }
    out.assign(begin, len);
    return true;
}

}  // namespace

EtwImageLoadMonitor::~EtwImageLoadMonitor() {
    stop();
}

void EtwImageLoadMonitor::setSessionName(const std::wstring& name) {
    sessionName_ = name;
}

ULONG EtwImageLoadMonitor::start(ImageLoadCallback imageCb) {
    if (running_.exchange(true)) {
        return ERROR_ALREADY_INITIALIZED;
    }
    imageCallback_ = std::move(imageCb);
    if (sessionName_.empty()) {
        sessionName_ = L"HookFramework-EtwMonitor";
    }
    return startInternal();
}

ULONG EtwImageLoadMonitor::startInternal() {
    const ULONG nameBytes = static_cast<ULONG>((sessionName_.size() + 1) * sizeof(wchar_t));
    const ULONG bufferSize = sizeof(EVENT_TRACE_PROPERTIES_V2) + nameBytes;

    auto buildProps = [&](std::vector<BYTE>& buffer) {
        buffer.assign(bufferSize, 0);
        auto* propsV2 = reinterpret_cast<PEVENT_TRACE_PROPERTIES_V2>(buffer.data());
        auto* props = reinterpret_cast<PEVENT_TRACE_PROPERTIES>(propsV2);

        propsV2->Wnode.BufferSize = bufferSize;
        propsV2->Wnode.Flags = WNODE_FLAG_TRACED_GUID | WNODE_FLAG_VERSIONED_PROPERTIES;
        propsV2->Wnode.ClientContext = 1;
        // Wnode.Guid must NOT be SystemTraceControlGuid when using a custom
        // session name; ETW generates a new GUID for a zeroed GUID.
        propsV2->LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE;
        propsV2->EnableFlags = EVENT_TRACE_FLAG_IMAGE_LOAD;
        propsV2->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES_V2);

        memcpy(buffer.data() + propsV2->LoggerNameOffset, sessionName_.c_str(), nameBytes);
        return props;
    };

    std::vector<BYTE> buffer;
    auto* props = buildProps(buffer);

    ULONG status = StartTraceW(&session_, sessionName_.c_str(), props);
    if (status == ERROR_ALREADY_EXISTS) {
        // A previous run (force-killed / crashed) left an orphaned session.
        spdlog::warn(L"ETW session '{}' already exists (orphaned by a previous run); "
                     L"stopping it and retrying",
                     sessionName_);
        ControlTraceW(0, sessionName_.c_str(), props, EVENT_TRACE_CONTROL_STOP);
        // ControlTraceW(STOP) overwrites the props buffer, so rebuild it before
        // retrying, otherwise the new session starts with no enabled events.
        props = buildProps(buffer);
        status = StartTraceW(&session_, sessionName_.c_str(), props);
    }
    if (status != ERROR_SUCCESS) {
        running_ = false;
        imageCallback_ = {};
        return status;
    }

    EVENT_TRACE_LOGFILEW logFile = {};
    logFile.LoggerName = const_cast<LPWSTR>(sessionName_.c_str());
    logFile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logFile.EventRecordCallback = &EtwImageLoadMonitor::eventRecordCallback;
    logFile.Context = this;

    trace_ = OpenTraceW(&logFile);
    if (trace_ == INVALID_PROCESSTRACE_HANDLE) {
        ULONG err = GetLastError();
        ControlTraceW(session_, sessionName_.c_str(), props, EVENT_TRACE_CONTROL_STOP);
        session_ = 0;
        running_ = false;
        imageCallback_ = {};
        return err;
    }

    consumerThread_ = std::thread([this]() {
        ProcessTrace(&trace_, 1, nullptr, nullptr);
    });

    return ERROR_SUCCESS;
}

void EtwImageLoadMonitor::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (trace_ != 0) {
        CloseTrace(trace_);  // unblocks ProcessTrace on the consumer thread
        trace_ = 0;
    }

    if (consumerThread_.joinable()) {
        consumerThread_.join();
    }

    if (session_ != 0) {
        ULONG bufferSize = sizeof(EVENT_TRACE_PROPERTIES) +
                           static_cast<ULONG>((sessionName_.size() + 1) * sizeof(wchar_t));
        std::vector<BYTE> buffer(bufferSize, 0);
        auto* props = reinterpret_cast<PEVENT_TRACE_PROPERTIES>(buffer.data());
        props->Wnode.BufferSize = bufferSize;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        ControlTraceW(session_, sessionName_.c_str(), props, EVENT_TRACE_CONTROL_STOP);
        session_ = 0;
    }

    imageCallback_ = {};
}

void WINAPI EtwImageLoadMonitor::eventRecordCallback(PEVENT_RECORD eventRecord) {
    auto* self = static_cast<EtwImageLoadMonitor*>(eventRecord->UserContext);
    if (!self) {
        return;
    }

    const GUID& provider = eventRecord->EventHeader.ProviderId;

    if (provider == kImageLoadGuid) {
        // EVENT_TRACE_TYPE_LOAD == 10; skip Unload(2)/DCStart(3)/DCEnd(4).
        if (eventRecord->EventHeader.EventDescriptor.Opcode != 10) {
            return;
        }

        const ULONG processId = eventRecord->EventHeader.ProcessId;
        const BYTE* data = static_cast<const BYTE*>(eventRecord->UserData);
        if (!data || eventRecord->UserDataLength < offsetof(ImageLoadEvent, FileName) + sizeof(WCHAR)) {
            return;
        }

        ULONG_PTR imageBase = reinterpret_cast<const ImageLoadEvent*>(data)->ImageBase;
        std::wstring filePath;
        if (!readWideString(data, eventRecord->UserDataLength, offsetof(ImageLoadEvent, FileName),
                            filePath)) {
            return;
        }

        if (self->imageCallback_) {
            self->imageCallback_(processId, imageBase, filePath);
        }
        return;
    }
}

}  // namespace hookfw
