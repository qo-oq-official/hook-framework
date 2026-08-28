// symbolize_trace：将已采集的 Perfetto trace 中记录的调用者模块+RVA，
// 借助 raw_pdb 解析回对应的函数名与源码位置(file:line)。
//
// 相比此前的 Python 实现(DIA2Dump 子进程 + 文本正则)：
//   1) PDB 解析改为进程内 raw_pdb，速度快且无需逐 PDB 拉起外部进程；
//   2) 全流程为原生 C++，日志使用 spdlog。
//
// 输出：
//   1) 符号化报告(JSON + 文本)；
//   2) 可选：注入 FUNC/SRC 注解的增强 trace，可在 Perfetto UI 直接查看。
//
// 用法：
//   symbolize_trace.exe --trace <trace> [--pdb-config <cfg.json>]
//       [--output <report.json>] [--augmented <out.trace>]

#include "logger.h"
#include "pdb_resolver.h"
#include "trace_augment.h"
#include "trace_reader.h"

#include <nlohmann/json.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kDefaultToolbox = R"(C:\Users\zwsoft\toolbox)";

// 简单命令行参数解析
struct Options {
    std::string trace;
    std::string pdbConfig;
    std::string output;
    std::string augmented;
    std::string toolbox = kDefaultToolbox;
    std::string traceProcessor;
    std::string traceconv;
    std::string cacheDir = "./tmp_cache";
    std::string logLevel = "info";
    bool noCache = false;
    bool help = false;
};

void PrintUsage() {
    std::cout <<
        "Usage: symbolize_trace.exe --trace <trace> [options]\n"
        "\n"
        "  --trace <path>          Input .perfetto-trace file (required)\n"
        "  --pdb-config <path>     JSON mapping module name -> PDB path\n"
        "  --output <path>         Report JSON (default: <trace>.resolved.json)\n"
        "  --augmented <path>      Optional output path for the augmented trace\n"
        "  --toolbox <dir>         Toolbox root dir (default: " << kDefaultToolbox << ")\n"
        "  --trace-processor <p>   Path to trace_processor_shell.exe\n"
        "  --traceconv <p>         Path to traceconv.exe\n"
        "  --cache-dir <dir>       PDB parse cache directory (default: ./tmp_cache)\n"
        "  --no-cache              Disable PDB parse cache\n"
        "  --log-level <lvl>       trace|debug|info|warn|error (default: info)\n"
        "  --help                  Show this help\n";
}

bool ParseArgs(int argc, char* argv[], Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 < argc) return argv[++i];
            return "";
        };
        if (arg == "--trace") opts.trace = next();
        else if (arg == "--pdb-config") opts.pdbConfig = next();
        else if (arg == "--output") opts.output = next();
        else if (arg == "--augmented") opts.augmented = next();
        else if (arg == "--toolbox") opts.toolbox = next();
        else if (arg == "--trace-processor") opts.traceProcessor = next();
        else if (arg == "--traceconv") opts.traceconv = next();
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

// 本工具所在目录（用于同目录查找外部工具）
std::string argv0Dir() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path().string();
}

// 查找工具可执行文件：优先显式路径，其次 toolbox，再次同目录，最后退回 PATH
std::string FindTool(const std::string& name, const std::string& overridePath, const std::string& toolbox) {
    if (!overridePath.empty()) {
        return overridePath;
    }
    std::vector<std::string> candidates = {
        (fs::path(toolbox) / "perfetto" / "windows-amd64" / name).string(),
        (fs::path(toolbox) / name).string(),
        (fs::path(argv0Dir()) / name).string(),
    };
    for (const auto& c : candidates) {
        if (fs::exists(c)) {
            return c;
        }
    }
    return name;  // 退回 PATH 查找
}

spdlog::level::level_enum ParseLevel(const std::string& s) {
    if (s == "trace") return spdlog::level::trace;
    if (s == "debug") return spdlog::level::debug;
    if (s == "warn") return spdlog::level::warn;
    if (s == "error") return spdlog::level::err;
    if (s == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

// 加载 pdb 配置：模块名(小写 basename) -> PDB 路径
std::unordered_map<std::string, std::string> LoadPdbConfig(const std::string& configPath) {
    std::unordered_map<std::string, std::string> map;
    if (configPath.empty()) {
        return map;
    }
    std::ifstream in(configPath);
    if (!in) {
        LOG_WARN("Cannot open pdb config: {}", configPath);
        return map;
    }
    nlohmann::json root;
    in >> root;
    for (const auto& item : root.value("pdb_config", nlohmann::json::array())) {
        std::string pdb = item.value("pdb_path", item.value("pdb", std::string("")));
        if (pdb.empty()) {
            continue;
        }
        std::string mod = item.value("module", item.value("dll_file", std::string("")));
        if (mod.empty()) {
            continue;
        }
        std::string base = fs::path(mod).filename().string();
        std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return std::tolower(c); });
        map[base] = pdb;
    }
    return map;
}

// 写 JSON + 文本报告
void WriteReport(const std::vector<hookfw::CallSite>& sites,
                 const std::vector<hookfw::ResolvedLocation>& locs,
                 const std::string& outputPath) {
    size_t resolved = 0;
    nlohmann::json entries = nlohmann::json::array();
    for (size_t i = 0; i < sites.size(); ++i) {
        const auto& s = sites[i];
        const auto& l = locs[i];
        if (!l.function.empty() || !l.file.empty()) {
            ++resolved;
        }
        nlohmann::json e;
        e["ts"] = s.ts;
        e["name"] = s.name;
        e["mod"] = s.mod;
        e["rva"] = s.rva;
        e["function"] = l.function;
        e["file"] = l.file;
        e["line"] = l.line;
        entries.push_back(std::move(e));
    }

    nlohmann::json doc;
    doc["summary"]["total"] = sites.size();
    doc["summary"]["resolved"] = resolved;
    doc["summary"]["unresolved"] = sites.size() - resolved;
    doc["entries"] = entries;

    std::ofstream out(outputPath);
    out << doc.dump(2);
    LOG_INFO("Report JSON written: {}", outputPath);

    // 文本报告
    fs::path txtPath = outputPath;
    txtPath.replace_extension(".txt");
    std::ofstream txt(txtPath);
    txt << "Resolved " << resolved << "/" << sites.size() << " call-sites ("
        << (sites.size() - resolved) << " unresolved)\n\n";
    for (size_t i = 0; i < sites.size(); ++i) {
        const auto& s = sites[i];
        const auto& l = locs[i];
        std::string locStr = l.file.empty() ? "<?>" : (l.file + ":" + std::to_string(l.line));
        std::string func = l.function.empty() ? "<unknown>" : l.function;
        txt << "  " << s.ts << "  " << fs::path(s.mod).filename().string()
            << "!0x" << std::hex << s.rva << std::dec
            << "  " << func << "  " << locStr << "\n";
    }
    LOG_INFO("Report text written: {}", txtPath.string());
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
    if (opts.trace.empty()) {
        std::cerr << "ERROR: --trace is required\n";
        PrintUsage();
        return 1;
    }

    // 初始化 spdlog 日志
    auto logger = std::make_shared<spdlog::logger>("symbolize_trace",
                                                   std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    logger->set_level(ParseLevel(opts.logLevel));
    logger->set_pattern("[%H:%M:%S.%e] [%l] %v");
    spdlog::register_logger(logger);

    if (!fs::exists(opts.trace)) {
        LOG_ERROR("Trace file not found: {}", opts.trace);
        return 1;
    }

    const std::string tpShell = FindTool("trace_processor_shell.exe", opts.traceProcessor, opts.toolbox);
    const std::string traceconv = FindTool("traceconv.exe", opts.traceconv, opts.toolbox);
    LOG_INFO("trace_processor_shell: {}", tpShell);
    LOG_INFO("traceconv: {}", traceconv);

    // 加载模块 -> PDB 映射
    auto pdbMap = LoadPdbConfig(opts.pdbConfig);
    LOG_INFO("PDB config: {} module(s)", pdbMap.size());

    // 1) 提取调用点
    LOG_INFO("Extracting call-sites from trace ...");
    std::vector<hookfw::CallSite> sites = hookfw::ExtractCallSites(opts.trace, tpShell);
    if (sites.empty()) {
        LOG_WARN("No call-sites found (missing debug.stack.RVA annotations?)");
        return 0;
    }

    // 2) 解析
    hookfw::PdbResolver resolver(std::move(pdbMap), opts.cacheDir, !opts.noCache);
    LOG_INFO("Resolving {} call-sites ...", sites.size());
    std::vector<hookfw::ResolvedLocation> locs;
    locs.reserve(sites.size());
    for (const auto& s : sites) {
        locs.push_back(resolver.Resolve(s.mod, static_cast<uint32_t>(s.rva)));
    }
    size_t resolved = 0;
    for (const auto& l : locs) {
        if (!l.function.empty() || !l.file.empty()) {
            ++resolved;
        }
    }
    LOG_INFO("Resolved {}/{} call-sites", resolved, sites.size());

    // 3) 写报告
    std::string output = opts.output;
    if (output.empty()) {
        output = fs::path(opts.trace).replace_extension(".resolved.json").string();
    }
    WriteReport(sites, locs, output);

    // 4) 增强 trace（可选）
    if (!opts.augmented.empty()) {
        LOG_INFO("Augmenting trace ...");
        if (hookfw::AugmentTrace(opts.trace, opts.augmented, resolver, traceconv, tpShell)) {
            LOG_INFO("Augmented trace written: {}", opts.augmented);
        } else {
            LOG_ERROR("Failed to augment trace");
            return 1;
        }
    }

    LOG_INFO("Done.");
    return 0;
}
