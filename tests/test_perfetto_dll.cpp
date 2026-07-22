#include "perfetto.h"
#include <winsock2.h>
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

#pragma comment(lib, "ws2_32.lib")

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
static std::atomic<bool> g_running{true};

static void perfettoLog(perfetto::base::LogMessageCallbackArgs args) {
    printf("[perfetto] %s:%d %s\n",
           args.filename ? args.filename : "?",
           args.line, args.message);
}

static void doStartTracing() {
    crashLog("[TCP] doStartTracing entered\n");
    printf("\n[TCP] start — Perfetto init...\n");

    perfetto::TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend;
    args.shmem_size_hint_kb = 4096;
    args.log_message_callback = &perfettoLog;
    perfetto::Tracing::Initialize(args);

    perfetto::DataSourceDescriptor desc;
    desc.set_name("test.DataSource");
    TestDataSource::Register(desc);

    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &tt);
    std::ostringstream oss;
    oss << "trace_tcp_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << ".perfetto";
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
    printf("[TCP] session started, trace=%s\n", g_tracePath.c_str());
    crashLog("[TCP] doStartTracing done\n");
}

static void doMultiThreaded() {
    crashLog("[TCP] doMultiThreaded entered\n");
    if (!g_session) { crashLog("[TCP] no session\n"); return; }

    static const int kThreads = 4;
    static const int kEvents = 100;
    std::atomic<int> total{0};
    std::vector<std::thread> threads;

    printf("[TCP] multi — %d threads x %d events...\n", kThreads, kEvents * 2);

    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([t, &total]() {
            for (int i = 0; i < kEvents; i++) {
                std::ostringstream name;
                name << "th_" << t << "_fn_" << i;

                TestDataSource::Trace([&, tid = GetCurrentThreadId()](auto ctx) {
                    thread_local bool s_desc = false;
                    if (!s_desc) {
                        uint32_t p = GetCurrentProcessId();
                        uint64_t u = (static_cast<uint64_t>(p) << 32) | (static_cast<uint64_t>(tid) & 0xFFFFFFFFULL);
                        auto dp = ctx.NewTracePacket();
                        auto* td = dp->set_track_descriptor();
                        td->set_uuid(u);
                        auto* thd = td->set_thread();
                        thd->set_pid(static_cast<int32_t>(p));
                        thd->set_tid(static_cast<int64_t>(tid));
                        s_desc = true;
                    }
                    uint32_t p = GetCurrentProcessId();
                    uint64_t u = (static_cast<uint64_t>(p) << 32) | (static_cast<uint64_t>(tid) & 0xFFFFFFFFULL);
                    auto pkt = ctx.NewTracePacket();
                    pkt->set_timestamp(static_cast<uint64_t>(perfetto::base::GetWallTimeNs().count()));
                    auto* te = pkt->set_track_event();
                    te->set_track_uuid(u);
                    te->set_name(name.str());
                    te->set_type(TrackEvent::TYPE_SLICE_BEGIN);
                    te->add_categories("tcp_mt");
                });

                std::this_thread::sleep_for(std::chrono::milliseconds(2));

                TestDataSource::Trace([t, i, tid = GetCurrentThreadId()](auto ctx) {
                    uint32_t p = GetCurrentProcessId();
                    uint64_t u = (static_cast<uint64_t>(p) << 32) | (static_cast<uint64_t>(tid) & 0xFFFFFFFFULL);
                    auto pkt = ctx.NewTracePacket();
                    pkt->set_timestamp(static_cast<uint64_t>(perfetto::base::GetWallTimeNs().count()));
                    auto* te = pkt->set_track_event();
                    te->set_track_uuid(u);
                    te->set_name(std::string("th_") + std::to_string(t) + "_fn_" + std::to_string(i));
                    te->set_type(TrackEvent::TYPE_SLICE_END);
                    te->add_categories("tcp_mt");
                });

                total.fetch_add(2);
            }
        });
    }
    for (auto& t : threads) t.join();
    printf("[TCP] multi — %d events written\n", total.load());
    crashLog("[TCP] doMultiThreaded done\n");
}

static void doShutdownAndSave() {
    crashLog("[TCP] doShutdown entered\n");
    if (!g_session) { crashLog("[TCP] no session\n"); return; }

    TestDataSource::Trace([](auto ctx) { ctx.Flush(); });

    g_session->FlushBlocking(5000);
    g_session->Stop();

    auto data = g_session->ReadTraceBlocking();
    if (!data.empty()) {
        std::filesystem::create_directories("dump_data");
        std::ofstream file(g_tracePath, std::ios::binary);
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        file.close();
        printf("[TCP] trace saved: %s (%zu bytes)\n", g_tracePath.c_str(), data.size());
    } else {
        printf("[TCP] trace is empty\n");
    }

    g_session.reset();
    perfetto::Tracing::Shutdown();
    crashLog("[TCP] doShutdown done\n");
}

static void tcpListener() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    bind(srv, (sockaddr*)&addr, sizeof(addr));
    listen(srv, 1);
    printf("[TCP] listener started on port 12345\n");
    crashLog("[TCP] listener started\n");

    while (g_running.load(std::memory_order_relaxed)) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(srv, &fds);
        timeval tv{1, 0};
        int r = select(0, &fds, nullptr, nullptr, &tv);
        if (r <= 0) continue;

        SOCKET cli = accept(srv, nullptr, nullptr);
        if (cli == INVALID_SOCKET) continue;

        char buf[64] = {};
        recv(cli, buf, sizeof(buf) - 1, 0);
        std::string cmd(buf);
        while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == '\n'))
            cmd.pop_back();

        crashLog(("[TCP] cmd: " + cmd + "\n").c_str());
        printf("[TCP] received command: %s\n", cmd.c_str());

        if (cmd == "start") {
            doStartTracing();
            send(cli, "ok\n", 3, 0);
        } else if (cmd == "multi") {
            doMultiThreaded();
            send(cli, "ok\n", 3, 0);
        } else if (cmd == "stop") {
            doShutdownAndSave();
            send(cli, "ok\n", 3, 0);
            closesocket(cli);
            g_running = false;
            break;
        } else {
            send(cli, "err\n", 4, 0);
        }
        closesocket(cli);
    }

    closesocket(srv);
    WSACleanup();
    crashLog("[TCP] listener stopped\n");
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
        printf("[DllMain] ATTACH — starting TCP listener on port 12345\n");
        std::thread(tcpListener).detach();
        Sleep(200);
        break;
    case DLL_PROCESS_DETACH:
        printf("[DllMain] DETACH — signaling listener to stop\n");
        g_running = false;
        crashLog("[DllMain] DETACH done\n");
        break;
    }
    return TRUE;
}
