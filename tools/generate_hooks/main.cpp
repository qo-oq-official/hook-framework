// generate_hooks：从 PDB 生成 hooks.json。
//
// 相比此前的 Python 实现(DIA2Dump 子进程 + 文本正则)：
//   1) 函数符号提取改为进程内 raw_pdb（见 pdb_common.h 的 CollectFunctionSymbols）；
//   2) 全流程为原生 C++，日志使用 spdlog。
//
// 对每个 pdb_config 条目：读取 pdb_path，收集函数符号(精简名 + RVA)，按
// function_white_list/function_black_list 正则过滤，按 RVA 去重并跳过 rva==0，
// 最终写出 hooks.json。
//
// 用法：
//   generate_hooks.exe --config <config.json> [--output <hooks.json>]
//       [--cache-dir <dir>] [--no-cache] [--log-level <lvl>]

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/ranges.h>

#include <pdb_common.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ── 日志 ──────────────────────────────────────────────────────────────
spdlog::logger* log() {
    std::shared_ptr<spdlog::logger> l = spdlog::get("generate_hooks");
    if (l) {
        return l.get();
    }
    return spdlog::default_logger_raw();
}

#define LOG_TRACE(...) do { auto* _l = log(); if (_l) _l->trace(__VA_ARGS__); } while (0)
#define LOG_DEBUG(...) do { auto* _l = log(); if (_l) _l->debug(__VA_ARGS__); } while (0)
#define LOG_INFO(...)  do { auto* _l = log(); if (_l) _l->info(__VA_ARGS__);  } while (0)
#define LOG_WARN(...)  do { auto* _l = log(); if (_l) _l->warn(__VA_ARGS__);  } while (0)
#define LOG_ERROR(...) do { auto* _l = log(); if (_l) _l->error(__VA_ARGS__); } while (0)

// ── 命令行参数 ────────────────────────────────────────────────────────
struct Options {
    std::string config;
    std::string output = "hooks_pdb.json";
    std::string cacheDir = "./tmp_cache";
    std::string logLevel = "info";
    bool noCache = false;
    bool help = false;
};

void PrintUsage() {
    std::cout <<
        "Usage: generate_hooks.exe --config <config.json> [options]\n"
        "\n"
        "  --config <path>     JSON config listing the PDBs to parse (required)\n"
        "  --output <path>     Output hooks.json (default: hooks_pdb.json)\n"
        "  --cache-dir <dir>   PDB parse cache directory (default: ./tmp_cache)\n"
        "  --no-cache          Disable PDB parse cache\n"
        "  --log-level <lvl>   trace|debug|info|warn|error (default: info)\n"
        "  --help              Show this help\n";
}

bool ParseArgs(int argc, char* argv[], Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 < argc) return argv[++i];
            return "";
        };
        if (arg == "--config") opts.config = next();
        else if (arg == "--output") opts.output = next();
        else if (arg == "--cache-dir") opts.cacheDir = next();
        else if (arg == "--no-cache") opts.noCache = true;
        else if (arg == "--log-level") opts.logLevel = next();
        else if (arg == "--help" || arg == "-h") opts.help = true;
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }
    return true;
}

spdlog::level::level_enum ParseLevel(const std::string& s) {
    if (s == "trace") return spdlog::level::trace;
    if (s == "debug") return spdlog::level::debug;
    if (s == "warn") return spdlog::level::warn;
    if (s == "err") return spdlog::level::err;
    if (s == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

class PatternMatcher {
private:
    std::vector<std::string> keywords; // 儲存拆解後的固定關鍵字
    bool hasLeadingDotStar = false;    // 開頭是否有 .*
    bool hasTrailingDotStar = false;   // 結尾是否有 .*

public:
    // 構造函數：解析通用 Pattern (例如 ".*Abc.*dFg.*" 或 "Abc.*dFg")
    PatternMatcher(const std::string& pattern) {
        if (pattern.empty()) return;

        std::string_view pv(pattern);

        // 1. 檢查並標記開頭的 .*
        if (pv.rfind(".*", 0) == 0) { // 等價於 starts_with(".*")
            hasLeadingDotStar = true;
            pv.remove_prefix(2);
        }

        // 2. 檢查並標記結尾的 .*
        if (pv.size() >= 2 && pv.substr(pv.size() - 2) == ".*") { // 等價於 ends_with(".*")
            hasTrailingDotStar = true;
            pv.remove_suffix(2);
        }

        // 3. 根據 ".*" 拆分出中間所有的固定關鍵字
        size_t pos = 0;
        while ((pos = pv.find(".*")) != std::string_view::npos) {
            if (pos > 0) {
                keywords.emplace_back(pv.substr(0, pos));
            }
            pv.remove_prefix(pos + 2);
        }
        if (!pv.empty()) {
            keywords.emplace_back(pv);
        }
        LOG_INFO("Pattern [{}] parse to {} dot-head [{}] dot-tail [{}]", pattern, keywords, hasLeadingDotStar, hasTrailingDotStar);
    }

    // 核心匹配與裁剪函數：返回 string_view 避免拷貝開銷
    bool isMatch(std::string_view text) const {
        if (keywords.empty()) {
            return hasLeadingDotStar;
        }

        size_t firstKeyPos = std::string_view::npos;
        size_t lastKeyEndPos = 0;
        size_t currentSearchPos = 0;

        // 按順序尋找所有關鍵字 (探測鏈)
        for (size_t i = 0; i < keywords.size(); ++i) {
            size_t pos = text.find(keywords[i], currentSearchPos);
            if (pos == std::string_view::npos) {
                return false; // 任何一個關鍵字沒按順序對上，即匹配失敗
            }

            // 記錄第一個關鍵字的起點
            if (i == 0 && !hasLeadingDotStar && pos != 0) {
                return false;
            }
            
            // 更新當前關鍵字的終點，並作為下一個關鍵字的搜尋起點
            lastKeyEndPos = pos + keywords[i].length();
            currentSearchPos = lastKeyEndPos;
        }
        if (!hasTrailingDotStar) {
            return text.length() == lastKeyEndPos;
        }

        // 返回裁剪後的視圖
        return true;
    }
};

std::vector<PatternMatcher> ParseMultiRegexToPatterns(std::vector<std::string> regexs) {
    std::vector<PatternMatcher> res;
    for (const std::string & reg : regexs) {
        res.push_back(PatternMatcher(reg));
    }
    return res;
}

// ── 缓存 ──────────────────────────────────────────────────────────────
std::string CacheKey(const std::string& pdbPath) {
    fs::path p(pdbPath);
    return std::to_string(fs::file_size(p)) + "-" +
           std::to_string(fs::last_write_time(p).time_since_epoch().count());
}

fs::path CachePath(const std::string& cacheDir, const std::string& pdbPath) {
    fs::path p(pdbPath);
    return fs::path(cacheDir) / (p.stem().string() + "-" + CacheKey(pdbPath) + ".json");
}

bool LoadCache(const fs::path& cachePath, std::vector<hookfw::FuncSymbol>& funcs) {
    try {
        std::ifstream in(cachePath);
        nlohmann::json j;
        in >> j;
        funcs.clear();
        for (const auto& item : j) {
            funcs.push_back({item[0].get<uint32_t>(), item[1].get<std::string>()});
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void SaveCache(const fs::path& cachePath, const std::vector<hookfw::FuncSymbol>& funcs) {
    try {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& f : funcs) {
            j.push_back({f.rva, f.name});
        }
        std::ofstream out(cachePath);
        out << j.dump();
    } catch (const std::exception& ex) {
        LOG_WARN("Failed to write cache {}: {}", cachePath.string(), ex.what());
    }
}

// ── 函数符号收集（含缓存） ────────────────────────────────────────────
bool CollectFunctions(const std::string& pdbPath,
                      const std::string& cacheDir,
                      bool useCache,
                      std::vector<hookfw::FuncSymbol>& funcs) {
    LOG_INFO("Collecting {} functions", pdbPath);
    if (useCache) {
        fs::path cachePath = CachePath(cacheDir, pdbPath);
        if (fs::exists(cachePath)) {
            if (LoadCache(cachePath, funcs)) {
                LOG_INFO("Read PDB cache: {} {}", pdbPath, cachePath.string());
                return true;
            }
            LOG_WARN("PDB cache corrupted ({}), re-parsing", cachePath.string());
        }
    }

    LOG_INFO("Parsing PDB: {}", pdbPath);

    hookfw::MappedFile file(pdbPath);
    if (!file.ok()) {
        LOG_ERROR("Cannot memory-map PDB file: {}", pdbPath);
        return false;
    }
    if (!hookfw::PdbOk(PDB::ValidateFile(file.data(), file.size()))) {
        LOG_ERROR("Invalid PDB file: {}", pdbPath);
        return false;
    }

    const PDB::RawFile rawFile = PDB::CreateRawFile(file.data());
    if (!hookfw::PdbOk(PDB::HasValidDBIStream(rawFile))) {
        LOG_ERROR("PDB has no valid DBI stream: {}", pdbPath);
        return false;
    }

    const PDB::DBIStream dbiStream = PDB::CreateDBIStream(rawFile);
    if (!hookfw::PdbOk(dbiStream.HasValidImageSectionStream(rawFile)) ||
        !hookfw::PdbOk(dbiStream.HasValidSymbolRecordStream(rawFile)) ||
        !hookfw::PdbOk(dbiStream.HasValidPublicSymbolStream(rawFile))) {
        LOG_ERROR("PDB is missing required sub-streams: {}", pdbPath);
        return false;
    }

    funcs = hookfw::CollectFunctionSymbols(rawFile, dbiStream);
    LOG_INFO("Collected {} function symbols from {}", funcs.size(), pdbPath);

    if (useCache) {
        fs::create_directories(cacheDir);
        SaveCache(CachePath(cacheDir, pdbPath), funcs);
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    Options opts;
    if (!ParseArgs(argc, argv, opts)) {
        PrintUsage();
        return 1;
    }
    if (opts.help) {
        PrintUsage();
        return 0;
    }
    if (opts.config.empty()) {
        std::cerr << "ERROR: --config is required\n";
        PrintUsage();
        return 1;
    }

    // 初始化 spdlog
    auto logger = std::make_shared<spdlog::logger>("generate_hooks",
                                                   std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    logger->set_level(ParseLevel(opts.logLevel));
    logger->set_pattern("[%H:%M:%S.%e] [%l] %v");
    spdlog::register_logger(logger);

    // 读取配置
    std::ifstream configIn(opts.config);
    if (!configIn) {
        LOG_ERROR("Cannot open config file: {}", opts.config);
        return 1;
    }
    nlohmann::json config;
    configIn >> config;

    nlohmann::json hooks = nlohmann::json::array();
    size_t totalStatic = 0;

    for (const auto& item : config.value("pdb_config", nlohmann::json::array())) {
        std::string pdbPath = item.value("pdb_path", std::string(""));
        if (pdbPath.empty() || !fs::exists(pdbPath)) {
            LOG_WARN("Not found PDB file: {}", pdbPath);
            continue;
        }

        std::vector<std::string> whiteList = item.value("function_white_list", std::vector<std::string>{});
        std::vector<std::string> blackList = item.value("function_black_list", std::vector<std::string>{});
        LOG_INFO("Parser whitelist");
        std::vector<PatternMatcher> whiteList_pattern = ParseMultiRegexToPatterns(whiteList);
        LOG_INFO("Parser blacklist");
        std::vector<PatternMatcher> blackList_pattern = ParseMultiRegexToPatterns(blackList);
        if (whiteList.empty()) {
            LOG_INFO("Skipping {} (empty function_white_list)", pdbPath);
            continue;
        }

        std::string module = fs::path(item.value("module", std::string(""))).filename().string();

        // 收集函数符号
        std::vector<hookfw::FuncSymbol> funcs;
        if (!CollectFunctions(pdbPath, opts.cacheDir, !opts.noCache, funcs)) {
            LOG_INFO("Failed to collect {}", pdbPath);
            continue;
        }

        // 白/黑名单过滤
        std::unordered_set<uint32_t> seenRva;

        size_t subCount = 0;
        for (const auto& f : funcs) {
            bool hit = false;
            for (const auto& re : whiteList_pattern) {
                if (re.isMatch(f.name)) {
                    hit = true;
                    break;
                }
            }
            if (hit) {
                for (const auto& re : blackList_pattern) {
                    if (re.isMatch(f.name)) {
                        hit = false;
                        break;
                    }
                }
            }
            if (!hit) {
                continue;
            }
            if (f.rva == 0 || seenRva.count(f.rva)) {
                LOG_DEBUG("skip {} rva=0x{:x}", f.name, f.rva);
                continue;
            }
            seenRva.insert(f.rva);

            nlohmann::json hook;
            hook["module"] = module;
            hook["rva"] = std::format("0x{:x}", f.rva);
            hook["action"] = "capture";
            hook["mode"] = "trace";
            hook["collector"] = "typed";
            hook["display_name"] = f.name;
            hooks.push_back(std::move(hook));

            if (f.name.find("static") != std::string::npos) {
                ++totalStatic;
            }
            ++subCount;
        }
        LOG_INFO("Found {} functions in {}", subCount, pdbPath);
    }

    nlohmann::json root;
    root["output_dir"] = "dump_data";
    root["output_format"] = "perfetto";
    root["hooks"] = hooks;

    std::ofstream out(opts.output);
    out << root.dump(2);

    const size_t total = hooks.size();
    LOG_INFO("export: {}, static: {}", total - totalStatic, totalStatic);
    LOG_INFO("Written: {}", opts.output);
    return 0;
}
