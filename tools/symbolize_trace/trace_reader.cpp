#include "trace_reader.h"

#include "logger.h"
#include "proc_util.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace hookfw {

namespace {

// 解析一行 CSV（支持带引号字段，字段内逗号）
std::vector<std::string> ParseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string cur;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                cur += c;
            }
        } else {
            if (c == '"') {
                inQuotes = true;
            } else if (c == ',') {
                fields.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
    }
    fields.push_back(cur);
    return fields;
}

}  // namespace

std::vector<CallSite> ExtractCallSites(const std::string& tracePath, const std::string& tpShell) {
    std::vector<CallSite> result;

    const std::string sql =
        "SELECT s.id, s.ts, s.name, "
        "(SELECT a.display_value FROM args a WHERE a.arg_set_id=s.arg_set_id AND a.key='debug.stack.RVA') AS rva, "
        "(SELECT a.display_value FROM args a WHERE a.arg_set_id=s.arg_set_id AND a.key='debug.stack.MOD') AS mod "
        "FROM slice s "
        "WHERE EXISTS (SELECT 1 FROM args a WHERE a.arg_set_id=s.arg_set_id AND a.key='debug.stack.RVA') "
        "ORDER BY s.id";

    // 将 SQL 写入临时文件，避免命令行引号转义问题
    std::filesystem::path sqlPath =
        std::filesystem::temp_directory_path() /
        ("symbolize_query_" + std::to_string(GetCurrentProcessId()) + ".sql");
    {
        std::ofstream f(sqlPath);
        f << sql;
    }

    std::string out;
    const int rc = RunProcessCapture(tpShell, {"query", "-f", sqlPath.string(), tracePath}, out);
    std::error_code ec;
    std::filesystem::remove(sqlPath, ec);

    if (rc != 0 || out.empty()) {
        LOG_ERROR("trace_processor query failed (rc={})", rc);
        return result;
    }

    // 解析 CSV 输出：首行为表头，其余为数据行
    std::istringstream iss(out);
    std::string line;
    bool header = true;
    std::unordered_map<std::string, int> colIdx;

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string> fields = ParseCsvLine(line);
        if (header) {
            for (size_t i = 0; i < fields.size(); ++i) {
                colIdx[fields[i]] = static_cast<int>(i);
            }
            header = false;
            continue;
        }

        auto get = [&](const char* name) -> const std::string& {
            auto it = colIdx.find(name);
            if (it == colIdx.end() || it->second >= static_cast<int>(fields.size())) {
                static const std::string empty;
                return empty;
            }
            return fields[it->second];
        };

        const std::string& tsStr = get("ts");
        const std::string& nameStr = get("name");
        const std::string& modStr = get("mod");
        const std::string& rvaStr = get("rva");
        if (tsStr.empty() || modStr.empty() || rvaStr.empty()) {
            continue;  // 缺失字段的行直接跳过
        }

        CallSite cs;
        try {
            cs.ts = std::stoull(tsStr);
            cs.rva = std::stoull(rvaStr);
        } catch (const std::exception&) {
            continue;
        }
        cs.name = nameStr;
        cs.mod = modStr;
        result.push_back(std::move(cs));
    }

    LOG_INFO("Extracted {} call-sites from trace", result.size());
    return result;
}

}  // namespace hookfw
