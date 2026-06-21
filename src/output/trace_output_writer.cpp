#include "output/trace_output_writer.h"
#include "logging/logger.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <vector>
#include <algorithm>

#pragma comment(lib, "dbghelp.lib")

namespace hookfw {

static void rotateTraceFiles(const std::string& dir, int keep) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        auto name = e.path().filename().string();
        if (name.starts_with("trace_") && name.ends_with(".json")) {
            files.push_back(e.path());
        }
    }
    if (ec) return;
    std::sort(files.begin(), files.end());
    while (static_cast<int>(files.size()) > keep) {
        fs::remove(files.front(), ec);
        files.erase(files.begin());
    }
}

TraceOutputWriter::TraceOutputWriter(const std::string& outputDir)
    : outputDir_(outputDir) {
    std::filesystem::create_directories(outputDir);

    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &tt);
    std::ostringstream oss;
    oss << "trace_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << ".json";
    traceFilePath_ = (std::filesystem::path(outputDir) / oss.str()).string();

    file_.open(traceFilePath_, std::ios::out | std::ios::trunc);
    if (file_.is_open()) {
        file_ << "{\"traceEvents\":[]}";
        file_.flush();
        dataEndPos_ = 16;
    }

    rotateTraceFiles(outputDir, 5);

    LOG_INFO("TraceOutputWriter created, output={}", traceFilePath_);
}

TraceOutputWriter::~TraceOutputWriter() {
    if (!finalized_) finalize();
    file_.close();
}

void TraceOutputWriter::finalize() {
    if (finalized_) return;
    flush();
    finalized_ = true;
    LOG_INFO("Trace file finalized: {} ({} events)", traceFilePath_, eventCount_);
}

double TraceOutputWriter::nowMicroseconds() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count() / 1000.0;
}

std::string TraceOutputWriter::undecorateName(const std::string& mangledName) {
    if (mangledName.empty() || mangledName[0] != '?') {
        return mangledName;
    }
    char buf[1024] = {};
    DWORD flags = UNDNAME_NO_ACCESS_SPECIFIERS |
                  UNDNAME_NO_MEMBER_TYPE |
                  UNDNAME_NO_MS_KEYWORDS |
                  UNDNAME_NO_RETURN_UDT_MODEL |
                  UNDNAME_NO_THISTYPE |
                  UNDNAME_NO_ALLOCATION_MODEL;
    DWORD result = UnDecorateSymbolName(mangledName.c_str(), buf, sizeof(buf), flags);
    if (result > 0) {
        return std::string(buf);
    }
    LOG_WARN("UnDecorateSymbolName failed (err=0x{:08X}), keeping raw: {}", GetLastError(), mangledName);
    return mangledName;
}

void TraceOutputWriter::addTraceEvent(const std::string& name,
                                      const std::string& category,
                                      char phase,
                                      double timestampUs,
                                      uint32_t pid,
                                      uint32_t tid,
                                      const nlohmann::json& args) {
    std::lock_guard lock(mutex_);
    events_.push_back({name, category, phase, timestampUs, pid, tid, args});
    if (events_.size() >= 200000) {
        LOG_WARN("TraceOutputWriter: {} events queued without flush, risk of OOM", events_.size());
    }
}

bool TraceOutputWriter::writeRaw(const std::string&, const std::string&,
                                 uint64_t, const std::string&,
                                 const uint8_t*, size_t) {
    return true;
}

bool TraceOutputWriter::writeJson(const std::string& moduleName,
                                  const std::string& functionName,
                                  uint64_t,
                                  const std::string& phase,
                                  const nlohmann::json& data) {
    char ph = (phase == "entry") ? 'B' : 'E';
    double ts = nowMicroseconds();
    uint32_t pid = GetCurrentProcessId();
    uint32_t tid = GetCurrentThreadId();

    std::string displayName = undecorateName(functionName);

    nlohmann::json args;
    if (data.contains("params")) {
        for (const auto& p : data["params"]) {
            std::string key = p.value("name", "arg");
            if (p.contains("value")) {
                args[key] = p["value"];
            } else if (p.contains("value_hex")) {
                args[key] = p["value_hex"];
            }
        }
    }
    if (data.contains("return_value") && data["return_value"].contains("value")) {
        args["return"] = data["return_value"]["value"];
    }
    if (data.contains("output_params")) {
        for (const auto& op : data["output_params"]) {
            std::string key = op.value("name", "out") + "_out";
            if (op.contains("deref_value")) {
                args[key] = op["deref_value"];
            }
        }
    }
    if (data.contains("exit_params")) {
        for (const auto& p : data["exit_params"]) {
            std::string key = p.value("name", "arg");
            if (p.contains("value")) {
                args[key] = p["value"];
            } else if (p.contains("value_hex")) {
                args[key] = p["value_hex"];
            }
        }
    }

    addTraceEvent(displayName, moduleName, ph, ts, pid, tid, args);
    return true;
}

uint64_t TraceOutputWriter::nextSeq() {
    return ++seqCounter_;
}

void TraceOutputWriter::flush() {
    std::lock_guard lock(mutex_);
    doFlush();
}

void TraceOutputWriter::doFlush() {
    if (events_.empty()) return;
    if (!file_.is_open()) return;

    try {
        file_.seekp(dataEndPos_);

        for (const auto& ev : events_) {
            nlohmann::json j;
            j["name"] = ev.name;
            j["cat"] = ev.category;
            j["ph"] = std::string(1, ev.phase);
            j["ts"] = ev.timestampUs;
            j["pid"] = ev.pid;
            j["tid"] = ev.tid;
            if (!ev.args.empty()) {
                j["args"] = ev.args;
            }
            if (eventCount_ > 0) {
                file_ << ",\n";
            }
            file_ << j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            eventCount_++;
        }

        dataEndPos_ = file_.tellp();
        file_ << "\n]}";
        file_.flush();
        events_.clear();
    } catch (const std::exception& ex) {
        LOG_ERROR("Trace flush failed: {}", ex.what());
    }
}

}
