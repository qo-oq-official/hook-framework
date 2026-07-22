#include "perfetto.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <Windows.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

using namespace perfetto::protos::pbzero;

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

// Called from EXE AFTER DllMain returns (simulates lazy init on first hook fire)
extern "C" __declspec(dllexport) void StartTracing() {
    crashLog("[StartTracing] entered\n");
    printf("\n[StartTracing] Perfetto init (after DllMain returned)...\n");

    perfetto::TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend;
    args.shmem_size_hint_kb = 4096;
    args.log_message_callback = &perfettoLog;
    perfetto::Tracing::Initialize(args);
    crashLog("[StartTracing] Initialize done\n");
    CHECK(true, "Tracing::Initialize OK");

    perfetto::DataSourceDescriptor desc;
    desc.set_name("test.DataSource");
    CHECK(TestDataSource::Register(desc), "DataSource registered");
    crashLog("[StartTracing] Register done\n");

    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &tt);
    std::ostringstream oss;
    oss << "trace_dll_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << ".perfetto";
    g_tracePath = (std::filesystem::path("dump_data") / oss.str()).string();

    perfetto::TraceConfig cfg;
    cfg.add_buffers()->set_size_kb(4096);
    auto* ds = cfg.add_data_sources()->mutable_config();
    ds->set_name("test.DataSource");
    cfg.set_duration_ms(0);
    cfg.set_flush_period_ms(10000);

    g_session = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
    g_session->Setup(cfg);
    g_session->Start();
    crashLog("[StartTracing] session started\n");
    CHECK(true, "Session started");
    printf("       trace file: %s\n", g_tracePath.c_str());
}

// Called from EXE AFTER StartTracing (simulates hook calls from multiple SW threads)
extern "C" __declspec(dllexport) void WriteMultiThreaded() {
    crashLog("[WriteMultiThreaded] entered\n");
    if (!g_session) { crashLog("[WriteMultiThreaded] no session\n"); return; }

    static const int kThreads = 4;
    static const int kEventsPerThread = 100;
    std::atomic<int> total{0};
    std::vector<std::thread> threads;

    printf("[WriteMultiThreaded] %d threads x %d BEGIN/END pairs each...\n",
           kThreads, kEventsPerThread);

    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([t, &total]() {
            for (int i = 0; i < kEventsPerThread; i++) {
                std::ostringstream name;
                name << "thread_" << t << "_fn_" << i;

                // BEGIN
                TestDataSource::Trace([&](TestDataSource::TraceContext ctx) {
                    auto pkt = ctx.NewTracePacket();
                    pkt->set_timestamp(
                        static_cast<uint64_t>(perfetto::base::GetWallTimeNs().count()));
                    auto* te = pkt->set_track_event();
                    te->set_name(name.str());
                    te->set_type(TrackEvent::TYPE_SLICE_BEGIN);
                    te->add_categories("poc_mt");
                    auto* da = te->add_debug_annotations();
                    da->set_name("thread");
                    da->set_int_value(t);
                    da = te->add_debug_annotations();
                    da->set_name("seq");
                    da->set_int_value(i);
                });

                std::this_thread::sleep_for(std::chrono::milliseconds(2));

                // END
                TestDataSource::Trace([t, i, &name](TestDataSource::TraceContext ctx) {
                    auto pkt = ctx.NewTracePacket();
                    pkt->set_timestamp(
                        static_cast<uint64_t>(perfetto::base::GetWallTimeNs().count()));
                    auto* te = pkt->set_track_event();
                    te->set_name(name.str());
                    te->set_type(TrackEvent::TYPE_SLICE_END);
                    te->add_categories("poc_mt");
                    auto* da = te->add_debug_annotations();
                    da->set_name("thread");
                    da->set_int_value(t);
                });

                int ev = total.fetch_add(2) + 2;
                if (i % 25 == 24)
                    printf("    thread %d: %d/%d events\n", t, ev, kEventsPerThread * 2);
            }
        });
    }
    for (auto& t : threads) t.join();

    int ev = total.load();
    CHECK(ev == kThreads * kEventsPerThread * 2, "all events written");
    printf("[WriteMultiThreaded] %d total events\n", ev);
    crashLog("[WriteMultiThreaded] done\n");
}

// Called from EXE BEFORE FreeLibrary (simulates DllMain DETACH shutdown)
extern "C" __declspec(dllexport) void ShutdownAndSave() {
    crashLog("[ShutdownAndSave] entered\n");
    if (!g_session) { crashLog("[ShutdownAndSave] no session\n"); return; }

    crashLog("[ShutdownAndSave] ds flush\n");
    TestDataSource::Trace([](TestDataSource::TraceContext ctx) {
        ctx.Flush();
    });

    crashLog("[ShutdownAndSave] FlushBlocking\n");
    bool flushed = g_session->FlushBlocking(5000);
    printf("       FlushBlocking = %s\n", flushed ? "true" : "false");

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
        printf("       trace saved: %s (%zu bytes)\n", g_tracePath.c_str(), data.size());
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
        printf("[DllMain] ATTACH — no Perfetto code (defer to StartTracing export)\n");
        break;
    case DLL_PROCESS_DETACH:
        printf("[DllMain] DETACH — nothing to do\n");
        crashLog("[DllMain] DETACH done\n");
        break;
    }
    return TRUE;
}
