#include "perfetto.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <Windows.h>
#include <io.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <sstream>

using namespace perfetto::protos::pbzero;

class TestDataSource : public perfetto::DataSource<TestDataSource> {
public:
    void OnSetup(const SetupArgs&) override {}
    void OnStart(const StartArgs&) override {}
    void OnStop(const StopArgs&) override {}
};

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(TestDataSource);

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

static std::atomic<bool> g_started{false};
static std::atomic<int> g_eventsWritten{0};
static const int kThreads = 4;
static const int kEventsPerThread = 200;

void threadFunc(int tid) {
    while (!g_started.load(std::memory_order_acquire))
        std::this_thread::yield();

    std::mt19937 rng(tid * 12345 + 67890);
    std::uniform_int_distribution<int> delay(1, 15);

    for (int i = 0; i < kEventsPerThread; i++) {
        std::ostringstream name;
        name << "thread_" << tid << "_fn_" << i;

        TestDataSource::Trace([&](TestDataSource::TraceContext ctx) {
            auto pkt = ctx.NewTracePacket();
            pkt->set_timestamp(
                static_cast<uint64_t>(perfetto::base::GetWallTimeNs().count()));
            auto* te = pkt->set_track_event();
            te->set_name(name.str());
            te->set_type(TrackEvent::TYPE_SLICE_BEGIN);
            te->add_categories("poc_mt");

            auto* da = te->add_debug_annotations();
            da->set_name("thread_id");
            da->set_int_value(tid);
            da = te->add_debug_annotations();
            da->set_name("event_index");
            da->set_int_value(i);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(delay(rng)));

        TestDataSource::Trace([&](TestDataSource::TraceContext ctx) {
            auto pkt = ctx.NewTracePacket();
            pkt->set_timestamp(
                static_cast<uint64_t>(perfetto::base::GetWallTimeNs().count()));
            auto* te = pkt->set_track_event();
            te->set_name(name.str());
            te->set_type(TrackEvent::TYPE_SLICE_END);
            te->add_categories("poc_mt");

            auto* da = te->add_debug_annotations();
            da->set_name("thread_id");
            da->set_int_value(tid);
        });

        g_eventsWritten.fetch_add(2, std::memory_order_relaxed);

        if (i % 50 == 0 && i > 0)
            printf("    thread %d: %d events written\n", tid, i * 2);
    }
}

int main() {
    printf("=== Perfetto Multi-Threaded PoC ===\n");
    printf("Threads: %d, Events per thread: %d, Total: %d\n\n",
           kThreads, kEventsPerThread, kThreads * kEventsPerThread * 2);

    // 1. Initialize
    printf("[1] Tracing::Initialize(kInProcessBackend)...\n");
    perfetto::TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend;
    args.shmem_size_hint_kb = 4096;
    args.log_message_callback = &perfettoLog;
    perfetto::Tracing::Initialize(args);
    CHECK(true, "Tracing::Initialize OK");

    // 2. Register data source
    printf("[2] Registering data source...\n");
    perfetto::DataSourceDescriptor desc;
    desc.set_name("test.DataSource");
    CHECK(TestDataSource::Register(desc), "DataSource registered");

    // 3. Create trace file for direct output (matching framework Setup(cfg, fd))
    printf("[3] Creating trace file for direct output...\n");
    std::filesystem::create_directories("dump_data");
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &tt);
    std::ostringstream oss;
    oss << "dump_data/test_poc_mt_" << std::put_time(&tm, "%H-%M-%S") << ".perfetto";
    std::string path = oss.str();

    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(hFile != INVALID_HANDLE_VALUE, "CreateFileA succeeded");
    printf("       trace file: %s\n", path.c_str());

    // 4. Setup session with fd (service writes directly to file)
    printf("[4] Creating session with Setup(cfg, fd)...\n");
    perfetto::TraceConfig cfg;
    cfg.add_buffers()->set_size_kb(4096);
    auto* ds = cfg.add_data_sources()->mutable_config();
    ds->set_name("test.DataSource");
    cfg.set_duration_ms(0);
    cfg.set_flush_period_ms(10000);

    auto session = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
    CHECK(session != nullptr, "NewTrace returned session");

    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hFile), 0);
    CHECK(fd >= 0, "_open_osfhandle succeeded");

    session->Setup(cfg, fd);
    session->Start();
    CHECK(true, "Session started with direct file output");

    // 5. Launch threads and collect
    printf("[5] Launching %d threads, %d events each...\n",
           kThreads, kEventsPerThread * 2);
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; i++)
        threads.emplace_back(threadFunc, i);

    Sleep(200);
    g_started.store(true, std::memory_order_release);

    for (auto& t : threads)
        t.join();

    int total = g_eventsWritten.load(std::memory_order_relaxed);
    CHECK(total == kThreads * kEventsPerThread * 2, "all events written");
    printf("       total events: %d\n", total);

    // 6. FlushBlocking (sync data to file)
    printf("[6] FlushBlocking(5000)...\n");
    fflush(stdout);
    bool flushed = session->FlushBlocking(5000);
    printf("       FlushBlocking result = %s\n", flushed ? "true" : "false");

    // 7. Stop + Shutdown (matching framework flow — NO ReadTraceBlocking)
    printf("[7] Stop...\n");
    session->Stop();
    CHECK(true, "Stop OK");

    printf("[8] Cleaning up (session.reset + Tracing::Shutdown + CloseHandle)...\n");
    session.reset();
    perfetto::Tracing::Shutdown();

    // Close the file — data was written by service during the session
    FlushFileBuffers(hFile);
    CloseHandle(hFile);

    // 9. Verify output file
    bool fileOk = std::filesystem::exists(path) &&
                  std::filesystem::file_size(path) > 0;
    CHECK(fileOk, "Perfetto file exists and non-empty");
    if (fileOk)
        printf("       file: %s (%zu bytes)\n", path.c_str(),
               static_cast<size_t>(std::filesystem::file_size(path)));

    CHECK(true, "Tracing::Shutdown OK");

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    printf("Open %s at https://ui.perfetto.dev\n", path.c_str());
    return g_fail;
}
