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

extern "C" __declspec(dllexport) void WriteMultiThreaded() {
    crashLog("[WriteMultiThreaded] entered\n");
    if (!g_session) { crashLog("[WriteMultiThreaded] no session\n"); return; }

    static const int kThreads = 4;
    static const int kEventsPerThread = 50;
    std::atomic<int> total{0};
    std::vector<std::thread> threads;

    printf("[WriteMultiThreaded] %d threads x %d events each...\n",
           kThreads, kEventsPerThread * 2);

    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([t, &total]() {
            for (int i = 0; i < kEventsPerThread; i++) {
                TestDataSource::Trace([&](TestDataSource::TraceContext ctx) {
                    auto pkt = ctx.NewTracePacket();
                    pkt->set_timestamp(
                        static_cast<uint64_t>(perfetto::base::GetWallTimeNs().count()));
                    auto* te = pkt->set_track_event();
                    te->set_name("mt_event");
                    te->set_type(perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_BEGIN);
                    te->add_categories("mt_test");
                    auto* da = te->add_debug_annotations();
                    da->set_name("thread");
                    da->set_int_value(t);
                    da = te->add_debug_annotations();
                    da->set_name("seq");
                    da->set_int_value(i);
                });
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                TestDataSource::Trace([t, i](TestDataSource::TraceContext ctx) {
                    auto pkt = ctx.NewTracePacket();
                    pkt->set_timestamp(
                        static_cast<uint64_t>(perfetto::base::GetWallTimeNs().count()));
                    auto* te = pkt->set_track_event();
                    te->set_name("mt_event");
                    te->set_type(perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_END);
                    te->add_categories("mt_test");
                    auto* da = te->add_debug_annotations();
                    da->set_name("thread");
                    da->set_int_value(t);
                });
                total.fetch_add(2);
                if (i == kEventsPerThread - 1)
                    printf("    thread %d done\n", t);
            }
        });
    }
    for (auto& t : threads) t.join();

    int ev = total.load();
    printf("[WriteMultiThreaded] %d events written\n", ev);
    crashLog("[WriteMultiThreaded] done\n");
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
    {
        DisableThreadLibraryCalls(nullptr);
        printf("[DllMain] ATTACH — running Perfetto init under loader lock\n");
        fflush(stdout);

        crashLog("[ATTACH] Tracing::Initialize\n");
        {
            perfetto::TracingInitArgs args;
            args.backends = perfetto::kInProcessBackend;
            args.shmem_size_hint_kb = 1024;
            args.log_message_callback = &perfettoLog;
            perfetto::Tracing::Initialize(args);
        }
        crashLog("[ATTACH] Initialize done\n");

        crashLog("[ATTACH] Register data source\n");
        {
            perfetto::DataSourceDescriptor desc;
            desc.set_name("test.DataSource");
            TestDataSource::Register(desc);
        }
        crashLog("[ATTACH] Register done\n");

        crashLog("[ATTACH] NewTrace + Setup + Start\n");
        {
            perfetto::TraceConfig cfg;
            cfg.add_buffers()->set_size_kb(1024);
            auto* ds = cfg.add_data_sources()->mutable_config();
            ds->set_name("test.DataSource");
            cfg.set_duration_ms(0);

            g_session = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
            g_session->Setup(cfg);
            g_session->Start();
        }
        crashLog("[ATTACH] session started\n");

        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &tt);
        std::ostringstream oss;
        oss << "trace_dllmain_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << ".perfetto";
        g_tracePath = (std::filesystem::path("dump_data") / oss.str()).string();

        crashLog("[ATTACH] writing events\n");
        for (int i = 0; i < 3; i++) {
            TestDataSource::Trace([i](TestDataSource::TraceContext ctx) {
                auto pkt = ctx.NewTracePacket();
                pkt->set_timestamp(
                    static_cast<uint64_t>(perfetto::base::GetWallTimeNs().count()));
                auto* te = pkt->set_track_event();
                te->set_name("dllmain_test_event");
                te->set_type(perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_BEGIN);
                te->add_categories("dllmain_test");
                auto* da = te->add_debug_annotations();
                da->set_name("iteration");
                da->set_int_value(i);
            });
            Sleep(100);
        }
        crashLog("[ATTACH] events written\n");
        printf("[DllMain] ATTACH — Perfetto init complete under loader lock\n");
        }  // end case block
        break;

    case DLL_PROCESS_DETACH:
        printf("[DllMain] DETACH — nothing to do\n");
        crashLog("[DllMain] DETACH done\n");
        break;
    }
    return TRUE;
}
