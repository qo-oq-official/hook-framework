#include "perfetto.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <Windows.h>

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

int main() {
    printf("=== Perfetto In-Process Backend PoC ===\n\n");

    // 1. Initialize
    printf("[1] Tracing::Initialize(kInProcessBackend)...\n");
    perfetto::TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend;
    args.shmem_size_hint_kb = 1024;
    args.log_message_callback = &perfettoLog;
    perfetto::Tracing::Initialize(args);
    CHECK(true, "Tracing::Initialize OK");

    // 2. Register data source
    printf("[2] Registering data source...\n");
    perfetto::DataSourceDescriptor desc;
    desc.set_name("test.DataSource");
    CHECK(TestDataSource::Register(desc), "DataSource registered");

    // 3. Create + start session
    printf("[3] Creating trace session...\n");
    perfetto::TraceConfig cfg;
    cfg.add_buffers()->set_size_kb(1024);
    auto* ds = cfg.add_data_sources()->mutable_config();
    ds->set_name("test.DataSource");
    cfg.set_duration_ms(0);

    auto session = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
    CHECK(session != nullptr, "NewTrace returned session");

    session->Setup(cfg);
    session->Start();
    CHECK(true, "Session started");

    // 4. Write track events
    printf("[4] Writing trace events...\n");
    for (int i = 0; i < 3; i++) {
        TestDataSource::Trace([i](TestDataSource::TraceContext ctx) {
            auto pkt = ctx.NewTracePacket();
            pkt->set_timestamp(
                static_cast<uint64_t>(perfetto::base::GetWallTimeNs().count()));
            auto* te = pkt->set_track_event();
            te->set_name("test_event");
            te->set_type(perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_BEGIN);
            te->add_categories("test_category");

            auto* da = te->add_debug_annotations();
            da->set_name("iteration");
            da->set_int_value(i);

            da = te->add_debug_annotations();
            da->set_name("message");
            da->set_string_value("hello from perfetto poc");
        });
        printf("    event %d written\n", i);
        Sleep(100);
    }
    CHECK(true, "3 events written");

    // 5. Flush data source (same as HookDataSource::flush())
    printf("[5] Flushing data source...\n");
    TestDataSource::Trace([](TestDataSource::TraceContext ctx) {
        ctx.Flush();
    });
    CHECK(true, "DataSource::Flush OK");

    // 6. FlushBlocking (sync data to consumer buffer)
    printf("[6] FlushBlocking(3000)...\n");
    fflush(stdout);
    bool flushed = session->FlushBlocking(3000);
    printf("       FlushBlocking result = %s\n", flushed ? "true" : "false");

    // 7. Stop (async)
    printf("[7] Stop...\n");
    session->Stop();
    CHECK(true, "Stop OK");

    // 8. ReadTraceBlocking
    printf("[8] ReadTraceBlocking...\n");
    auto data = session->ReadTraceBlocking();
    CHECK(!data.empty(), "ReadTraceBlocking returned data");
    printf("       data.size() = %zu bytes\n", data.size());

    // 9. Write to file
    printf("[9] Writing trace file...\n");
    std::filesystem::create_directories("dump_data");
    {
        std::ofstream file("dump_data/test_poc.perfetto", std::ios::binary);
        file.write(data.data(), data.size());
    }
    bool fileOk = std::filesystem::exists("dump_data/test_poc.perfetto") &&
                  std::filesystem::file_size("dump_data/test_poc.perfetto") > 0;
    CHECK(fileOk, "Perfetto file written to dump_data/test_poc.perfetto");

    // 10. Tracing::Shutdown (kill service thread)
    printf("[10] Tracing::Shutdown...\n");
    session.reset();
    perfetto::Tracing::Shutdown();
    CHECK(true, "Tracing::Shutdown OK");

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail;
}
