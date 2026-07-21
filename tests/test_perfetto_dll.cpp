#include "perfetto.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <Windows.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

class TestDataSource : public perfetto::DataSource<TestDataSource> {
public:
    void OnSetup(const SetupArgs&) override {}
    void OnStart(const StartArgs&) override {}
    void OnStop(const StopArgs&) override {}
};

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(TestDataSource);

static void crashLog(const char* msg);

static std::unique_ptr<perfetto::TracingSession> g_session;
static std::string g_tracePath;
static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while(0)

static void perfettoLog(perfetto::base::LogMessageCallbackArgs args) {
    printf("[perfetto] %s:%d %s\n",
           args.filename ? args.filename : "?",
           args.line, args.message);
}

extern "C" __declspec(dllexport) void StartTracing() {
    crashLog("[StartTracing] entered\n");
    printf("\n[StartTracing] Perfetto init + events...\n");

    perfetto::TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend;
    args.shmem_size_hint_kb = 1024;
    args.log_message_callback = &perfettoLog;
    perfetto::Tracing::Initialize(args);
    CHECK(true, "Tracing::Initialize OK");

    perfetto::DataSourceDescriptor desc;
    desc.set_name("test.DataSource");
    CHECK(TestDataSource::Register(desc), "DataSource registered");

    perfetto::TraceConfig cfg;
    cfg.add_buffers()->set_size_kb(1024);
    auto* ds = cfg.add_data_sources()->mutable_config();
    ds->set_name("test.DataSource");
    cfg.set_duration_ms(0);

    g_session = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
    g_session->Setup(cfg);
    g_session->Start();

    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &tt);
    std::ostringstream oss;
    oss << "trace_dll_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << ".perfetto";
    g_tracePath = (std::filesystem::path("dump_data") / oss.str()).string();

    for (int i = 0; i < 3; i++) {
        TestDataSource::Trace([i](TestDataSource::TraceContext ctx) {
            auto pkt = ctx.NewTracePacket();
            pkt->set_timestamp(
                static_cast<uint64_t>(perfetto::base::GetWallTimeNs().count()));
            auto* te = pkt->set_track_event();
            te->set_name("dll_test_event");
            te->set_type(perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_BEGIN);
            te->add_categories("dll_test");
            auto* da = te->add_debug_annotations();
            da->set_name("iteration");
            da->set_int_value(i);
        });
        printf("    dll event %d written\n", i);
        Sleep(100);
    }
    CHECK(true, "3 events written");
    crashLog("[StartTracing] done\n");
}

// Called from EXE BEFORE FreeLibrary (not from DllMain)
extern "C" __declspec(dllexport) void ShutdownAndSave() {
    crashLog("[ShutdownAndSave] entered\n");
    if (!g_session) { crashLog("[ShutdownAndSave] no session\n"); return; }

    crashLog("[ShutdownAndSave] ds flush\n");
    TestDataSource::Trace([](TestDataSource::TraceContext ctx) {
        ctx.Flush();
    });

    crashLog("[ShutdownAndSave] FlushBlocking\n");
    g_session->FlushBlocking(3000);

    crashLog("[ShutdownAndSave] Stop\n");
    g_session->Stop();

    crashLog("[ShutdownAndSave] ReadTraceBlocking\n");
    auto data = g_session->ReadTraceBlocking();
    CHECK(!data.empty(), "ReadTraceBlocking returned data");
    if (!data.empty()) {
        crashLog("[ShutdownAndSave] writing file\n");
        std::filesystem::create_directories("dump_data");
        std::ofstream file(g_tracePath, std::ios::binary);
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        file.close();
        CHECK(std::filesystem::file_size(g_tracePath) > 0, "Perfetto file written");
        printf("  trace saved: %s (%zu bytes)\n", g_tracePath.c_str(), data.size());
    }

    crashLog("[ShutdownAndSave] reset + Shutdown\n");
    g_session.reset();
    perfetto::Tracing::Shutdown();
    crashLog("[ShutdownAndSave] done\n");
}

static void crashLog(const char* msg) {
    HANDLE h = CreateFileA("perfetto_dll_crash.log", FILE_APPEND_DATA, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(h, msg, (DWORD)strlen(msg), &w, nullptr);
        CloseHandle(h);
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    crashLog("[DllMain] entered\n");
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(nullptr);
        printf("[DllMain] ATTACH (defer init to StartTracing export)\n");
        break;
    case DLL_PROCESS_DETACH:
        printf("[DllMain] DETACH — nothing to do (shutdown was called before FreeLibrary)\n");
        crashLog("[DllMain] DETACH done\n");
        break;
    }
    return TRUE;
}
