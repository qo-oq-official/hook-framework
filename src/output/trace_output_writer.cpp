#include "output/trace_output_writer.h"
#include "logging/logger.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <fstream>
#include <filesystem>
#include <chrono>

#pragma comment(lib, "dbghelp.lib")

namespace hookfw {

TraceOutputWriter::TraceOutputWriter(const std::string& outputDir)
    : outputDir_(outputDir) {
    std::filesystem::create_directories(outputDir);
    traceFilePath_ = (std::filesystem::path(outputDir) / "trace.json").string();

    {
        std::ofstream file(traceFilePath_, std::ios::trunc);
        if (file.is_open()) {
            file << "{\"traceEvents\":[]}";
            file.flush();
            nextWritePos_ = 16;
            headerWritten_ = true;
        }
    }

    LOG_INFO("TraceOutputWriter created, output={}", traceFilePath_);
}

TraceOutputWriter::~TraceOutputWriter() {
    flush();
}

int64_t TraceOutputWriter::nowMicroseconds() {
    auto now = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
    return us.count();
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
    return mangledName;
}

void TraceOutputWriter::addTraceEvent(const std::string& name,
                                      const std::string& category,
                                      char phase,
                                      int64_t timestampUs,
                                      uint32_t pid,
                                      uint32_t tid,
                                      const nlohmann::json& args) {
    std::lock_guard lock(mutex_);
    events_.push_back({name, category, phase, timestampUs, pid, tid, args});

    if (events_.size() >= flushThreshold_) {
        doFlush();
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
    int64_t ts = nowMicroseconds();
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

    try {
        std::fstream file(traceFilePath_, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("Cannot open trace file: {}", traceFilePath_);
            return;
        }

        file.seekp(nextWritePos_);

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
                file << ",\n";
            }
            file << j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            eventCount_++;
        }

        nextWritePos_ = file.tellp();
        file << "\n]}";
        file.flush();

        events_.clear();
    } catch (const std::exception& ex) {
        LOG_ERROR("Trace flush failed: {}", ex.what());
    }
}

}
