#include "dumpbin_parser.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <array>
#include <regex>
#include <cstdio>
#include <filesystem>
#include <format>

namespace hookfw {

bool DumpbinParser::parse(const std::string& dumpbinOutput) {
    entries_.clear();
    moduleName_.clear();

    std::istringstream stream(dumpbinOutput);
    std::string line;
    bool inExportSection = false;
    bool headerPassed = false;

    std::regex exportRegex(R"(\s+(\d+)\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+))");
    std::regex dllNameRegex(R"(Dump of file (.+))");

    while (std::getline(stream, line)) {
        std::smatch match;

        if (std::regex_search(line, match, dllNameRegex)) {
            std::filesystem::path p(match[1].str());
            moduleName_ = p.filename().string();
        }

        if (line.find("ordinal") != std::string::npos &&
            line.find("hint") != std::string::npos &&
            line.find("RVA") != std::string::npos) {
            inExportSection = true;
            headerPassed = false;
            continue;
        }

        if (inExportSection && !headerPassed) {
            headerPassed = true;
            continue;
        }

        if (inExportSection) {
            if (line.empty() || line.find("Summary") != std::string::npos) {
                inExportSection = false;
                continue;
            }

            if (std::regex_search(line, match, exportRegex)) {
                ExportEntry entry;
                entry.ordinal = std::stoul(match[1].str());
                entry.hint = std::stoul(match[2].str(), nullptr, 16);
                entry.rva = std::stoull(match[3].str(), nullptr, 16);
                entry.name = match[4].str();
                entries_.push_back(entry);
            }
        }
    }

    return !entries_.empty();
}

}

std::string runDumpbin(const std::string& modulePath) {
    std::string cmd = "dumpbin /exports \"" + modulePath + "\" 2>&1";
    std::array<char, 4096> buffer;
    std::string result;

    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "ERROR: Failed to run dumpbin. Ensure Visual Studio tools are in PATH.\n";
        return "";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }

    int exitCode = _pclose(pipe);
    if (exitCode != 0) {
        std::cerr << "WARNING: dumpbin exited with code " << exitCode << "\n";
    }

    return result;
}

int main(int argc, char* argv[]) {
    std::cout << "=== DUMPBIN Resolver ===\n";

    if (argc < 3) {
        std::cout << "Usage: dumpbin_resolver --module <path> --output <json_file>\n";
        std::cout << "  Runs 'dumpbin /exports' and generates hook config template.\n";
        return 1;
    }

    std::string modulePath;
    std::string outputPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--module" || arg == "-m") && i + 1 < argc) {
            modulePath = argv[++i];
        } else if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            outputPath = argv[++i];
        }
    }

    if (modulePath.empty() || outputPath.empty()) {
        std::cerr << "ERROR: --module and --output are required\n";
        return 1;
    }

    if (!std::filesystem::exists(modulePath)) {
        std::cerr << "ERROR: Module file not found: " << modulePath << "\n";
        return 1;
    }

    std::cout << "Running dumpbin /exports on: " << modulePath << "\n";
    std::string output = runDumpbin(modulePath);
    if (output.empty()) {
        std::cerr << "ERROR: dumpbin produced no output\n";
        return 1;
    }

    hookfw::DumpbinParser parser;
    if (!parser.parse(output)) {
        std::cerr << "ERROR: Failed to parse dumpbin output (no exports found)\n";
        std::cerr << "First 2000 chars of output:\n"
                  << output.substr(0, 2000) << "\n";
        return 1;
    }

    std::cout << "Found " << parser.entries().size() << " exports in "
              << parser.moduleName() << "\n";

    nlohmann::json root;
    root["output_dir"] = "dump_data";
    root["log_file"] = "hook_framework.log";
    root["log_level"] = "trace";

    nlohmann::json hooks = nlohmann::json::array();
    for (const auto& entry : parser.entries()) {
        nlohmann::json hook;
        hook["module"] = parser.moduleName();
        hook["symbol"] = entry.name;
        hook["rva"] = std::format("0x{:X}", entry.rva);
        hook["ordinal"] = entry.ordinal;
        hook["action"] = "capture";
        hook["collector"] = "raw";
        hook["params"] = nlohmann::json::array();
        hook["return_type"] = "int64";
        hooks.push_back(hook);
    }
    root["hooks"] = hooks;

    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        std::cerr << "ERROR: Cannot write to " << outputPath << "\n";
        return 1;
    }
    outFile << root.dump(2);
    std::cout << "Config template written to: " << outputPath << "\n";
    std::cout << "Edit the file to add param types for functions you want to capture.\n";
    return 0;
}
