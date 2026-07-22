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

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("network").SetDescription(""),
    perfetto::Category("graphics").SetDescription(""),
    perfetto::Category("gaming").SetDescription("")
);
PERFETTO_TRACK_EVENT_STATIC_STORAGE();
using namespace perfetto::protos::pbzero;

class TestDataSource : public perfetto::DataSource<TestDataSource> {
public:
    void OnSetup(const SetupArgs&) override {}
    void OnStart(const StartArgs&) override {}
    void OnStop(const StopArgs&) override {}
};

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(TestDataSource);

static void crashLog(const char* msg);

static void perfettoLog(perfetto::base::LogMessageCallbackArgs args) {
    printf("[perfetto] %s:%d %s\n",
           args.filename ? args.filename : "?",
           args.line, args.message);
}

extern "C" __declspec(dllexport)  void StartTracing() {
    crashLog("[DLL] doStartTracing entered\n");
    printf("\n[DLL] start — Perfetto init (kSystemBackend)...\n");

    perfetto::TracingInitArgs args;
    args.backends = perfetto::kSystemBackend;
    args.log_message_callback = &perfettoLog;
    perfetto::Tracing::Initialize(args);
    crashLog("[DLL] Initialize done\n");
    perfetto::TrackEvent::Register();
}

extern "C" __declspec(dllexport)  void MultiThreaded() {
    crashLog("[TCP] doMultiThreaded entered\n");


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


                TRACE_EVENT_BEGIN("network", perfetto::DynamicString{name.str()},  
                [&](perfetto::EventContext ctx) {
                    auto* debug_annotation = ctx.event()->add_debug_annotations();
                    debug_annotation->set_name("key_i");
                    debug_annotation->set_int_value(i);
                });
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                TRACE_EVENT_END("network",
                [&](perfetto::EventContext ctx) {
                    auto* debug_annotation = ctx.event()->add_debug_annotations();
                    debug_annotation->set_name("key_o");
                    debug_annotation->set_int_value(i);
                });

                total.fetch_add(2);
            }
        });
    }
    for (auto& t : threads) t.join();
    printf("[DLL] multi — %d events written\n", total.load());
    crashLog("[DLL] doMultiThreaded done\n");
}

extern "C" __declspec(dllexport)  void ShutdownAndSave() {
    crashLog("[DLL] doShutdown entered\n");
    crashLog("[TCP] doShutdown done\n");
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
        printf("[DllMain] ATTACH — starting TCP listener on port 12345\n");
        break;
    case DLL_PROCESS_DETACH:
        printf("[DllMain] DETACH — signaling listener to stop\n");
        crashLog("[DllMain] DETACH done\n");
        break;
    }
    return TRUE;
}
