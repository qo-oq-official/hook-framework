#include "trace_augment.h"

#include "logger.h"
#include "proc_util.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

namespace hookfw {

namespace {

// protobuf 文本格式字符串转义
std::string ProtoEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

// protobuf 文本格式字符串反转义
std::string ProtoUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char nxt = s[i + 1];
            switch (nxt) {
                case '\\': out += '\\'; ++i; break;
                case '"': out += '"'; ++i; break;
                case 'n': out += '\n'; ++i; break;
                case 'r': out += '\r'; ++i; break;
                case 't': out += '\t'; ++i; break;
                default: out += nxt; ++i; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

}  // namespace

size_t AugmentText(const std::string& text, PdbResolver& resolver, std::string& outText) {
    // 拆行（去掉行尾 CR，便于比较）
    std::vector<std::string> lines;
    {
        std::istringstream iss(text);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(line);
        }
    }

    static const std::regex kRvaRe("name:\\s*\"RVA\"\\s*uint_value:\\s*(\\d+)");
    static const std::regex kModRe("name:\\s*\"MOD\"\\s*string_value:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");

    size_t injected = 0;

    for (size_t i = 0; i < lines.size(); ++i) {
        // 定位 debug_annotations { 块起点
        const size_t pos = lines[i].find("debug_annotations {");
        if (pos == std::string::npos) {
            continue;
        }
        const std::string indent = lines[i].substr(0, pos);

        // 定位块结束（与块同缩进的 "}"）
        size_t close = i + 1;
        while (close < lines.size() && lines[close] != indent + "}") {
            ++close;
        }
        if (close >= lines.size()) {
            continue;
        }

        // 提取块体
        std::string body;
        for (size_t j = i + 1; j < close; ++j) {
            body += lines[j];
            body += '\n';
        }

        // 提取 RVA 与 MOD
        std::smatch rvaMatch;
        std::smatch modMatch;
        if (!std::regex_search(body, rvaMatch, kRvaRe) ||
            !std::regex_search(body, modMatch, kModRe)) {
            continue;  // 非栈信息块，跳过
        }

        uint32_t rva = 0;
        try {
            rva = static_cast<uint32_t>(std::stoul(rvaMatch[1].str()));
        } catch (const std::exception&) {
            continue;
        }
        const std::string mod = ProtoUnescape(modMatch[1].str());

        const ResolvedLocation loc = resolver.Resolve(mod, rva);
        if (loc.empty()) {
            continue;  // 解析失败，保持原样
        }

        // 构造待插入的 dict_entries（与 MOD 同级缩进）
        const std::string entryIndent = indent + "  ";
        const std::string fieldIndent = entryIndent + "  ";
        std::vector<std::string> toInsert;
        if (!loc.function.empty()) {
            toInsert.push_back(entryIndent + "dict_entries {");
            toInsert.push_back(fieldIndent + "name: \"FUNC\"");
            toInsert.push_back(fieldIndent + "string_value: \"" + ProtoEscape(loc.function) + "\"");
            toInsert.push_back(entryIndent + "}");
        }
        if (!loc.file.empty()) {
            const std::string src = loc.file + ":" + std::to_string(loc.line);
            toInsert.push_back(entryIndent + "dict_entries {");
            toInsert.push_back(fieldIndent + "name: \"SRC\"");
            toInsert.push_back(fieldIndent + "string_value: \"" + ProtoEscape(src) + "\"");
            toInsert.push_back(entryIndent + "}");
        }

        if (toInsert.empty()) {
            continue;
        }

        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(close), toInsert.begin(), toInsert.end());
        ++injected;
        i = close + toInsert.size();  // 跳到闭合花括号之后继续
    }

    std::ostringstream oss;
    for (const auto& l : lines) {
        oss << l << '\n';
    }
    outText = oss.str();
    return injected;
}

bool AugmentTrace(const std::string& tracePath,
                  const std::string& outputPath,
                  PdbResolver& resolver,
                  const std::string& traceconv,
                  const std::string& tpShell) {
    const std::string tmpText = outputPath + ".textproto";

    // 1) 转换为文本 proto
    std::string dummy;
    int rc = RunProcessCapture(traceconv, {"text", tracePath, tmpText}, dummy);
    if (rc != 0 || !std::filesystem::exists(tmpText) || std::filesystem::file_size(tmpText) == 0) {
        LOG_ERROR("traceconv text failed (rc={})", rc);
        return false;
    }

    // 2) 读取并注入
    std::ifstream in(tmpText, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    in.close();

    std::string outText;
    const size_t injected = AugmentText(buf.str(), resolver, outText);
    if (injected == 0) {
        LOG_WARN("no stack annotations found to augment, keeping original trace");
    } else {
        LOG_INFO("Injected FUNC/SRC annotations into {} slice(s)", injected);
    }

    std::ofstream out(tmpText, std::ios::binary);
    out << outText;
    out.close();

    // 3) 转回二进制 trace
    rc = RunProcessCapture(tpShell, {"convert", "binary", tmpText, outputPath}, dummy);
    std::error_code ec;
    std::filesystem::remove(tmpText, ec);

    if (rc != 0 || !std::filesystem::exists(outputPath)) {
        LOG_ERROR("convert binary failed (rc={})", rc);
        return false;
    }
    return true;
}

}  // namespace hookfw
