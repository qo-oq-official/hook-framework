#include "pdb_resolver.h"

#include "logger.h"
#include "pdb_common.h"

#include <PDB_InfoStream.h>
#include <PDB_ModuleLineStream.h>
#include <PDB_NamesStream.h>
#include "Foundation/PDB_PointerUtil.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace hookfw {

namespace {

// 从模块行信息流 + names 流中收集 (RVA -> 文件 + 行号)，结果按 RVA 升序。
// 逻辑移植自 raw_pdb 的 ExampleLines.cpp，并对文件名按 names 偏移去重以节省内存。
void BuildLines(const PDB::RawFile& rawFile, const PDB::DBIStream& dbiStream,
                const PDB::InfoStream& infoStream,
                std::vector<std::string>& files, std::vector<PdbResolver::LineEntry>& lines) {
    using namespace PDB::CodeView::DBI;

    if (!infoStream.HasNamesStream()) {
        LOG_WARN("PDB has no /names stream, line info unavailable");
        return;
    }

    const PDB::ImageSectionStream imageSectionStream = dbiStream.CreateImageSectionStream(rawFile);
    const PDB::ModuleInfoStream moduleInfoStream = dbiStream.CreateModuleInfoStream(rawFile);
    const PDB::NamesStream namesStream = infoStream.CreateNamesStream(rawFile);

    // 行信息中间结构：section 用于排序，rawLines 记录 (行号, 文件名条目索引)
    struct Section {
        uint16_t index;
        uint32_t offset;
        size_t lineIndex;
    };
    std::vector<Section> sections;
    std::vector<uint32_t> filenameOffsets;                        // 每个文件名条目 -> names 流偏移
    std::vector<std::pair<uint32_t, uint32_t>> rawLines;          // (行号, 文件名条目索引)

    for (const PDB::ModuleInfoStream::Module& module : moduleInfoStream.GetModules()) {
        if (!module.HasLineStream()) {
            continue;
        }
        const PDB::ModuleLineStream moduleLineStream = module.CreateLineStream(rawFile);
        const size_t moduleFilenamesStart = filenameOffsets.size();
        const FileChecksumHeader* moduleChecksumHeader = nullptr;

        moduleLineStream.ForEachSection([&](const LineSection* lineSection) {
            if (lineSection->header.kind == DebugSubsectionKind::S_LINES) {
                moduleLineStream.ForEachLinesBlock(lineSection,
                    [&](const LinesFileBlockHeader* blockHeader, const Line* blockLines, const Column*) {
                        if (blockHeader->numLines == 0) {
                            return;
                        }
                        const uint16_t sectionIndex = lineSection->linesHeader.sectionIndex;
                        const uint32_t sectionOffset = lineSection->linesHeader.sectionOffset;

                        // 先暂存 checksum 偏移，待解析完本模块的 S_FILECHECKSUMS 后再转换为 names 偏移
                        const size_t filenameEntry = filenameOffsets.size();
                        filenameOffsets.push_back(blockHeader->fileChecksumOffset);

                        sections.push_back({sectionIndex, sectionOffset, rawLines.size()});
                        rawLines.push_back({blockLines[0].linenumStart, static_cast<uint32_t>(filenameEntry)});

                        for (uint32_t i = 1; i < blockHeader->numLines; ++i) {
                            const Line& line = blockLines[i];
                            sections.push_back({sectionIndex, sectionOffset + line.offset, rawLines.size()});
                            rawLines.push_back({line.linenumStart, static_cast<uint32_t>(filenameEntry)});
                        }
                    });
            } else if (lineSection->header.kind == DebugSubsectionKind::S_FILECHECKSUMS) {
                moduleChecksumHeader = &lineSection->checksumHeader;
            }
            // S_INLINEELINES 无需处理（行内联信息不影响函数调用点定位）
        });

        // 将本模块各文件名条目的 checksum 偏移解析为 names 流偏移
        for (size_t i = moduleFilenamesStart; i < filenameOffsets.size(); ++i) {
            const FileChecksumHeader* ch =
                PDB::Pointer::Offset<const FileChecksumHeader*>(moduleChecksumHeader, filenameOffsets[i]);
            filenameOffsets[i] = ch->filenameOffset;
        }
    }

    // 按 (section, offset) 排序，便于顺序迭代
    std::sort(sections.begin(), sections.end(), [](const Section& a, const Section& b) {
        if (a.index == b.index) {
            return a.offset < b.offset;
        }
        return a.index < b.index;
    });

    // 展开为 (rva, 行号, 文件索引)，文件名按 names 偏移去重
    std::unordered_map<uint32_t, uint32_t> fileIndexByOffset;
    for (const Section& s : sections) {
        const auto& [lineNumber, filenameEntry] = rawLines[s.lineIndex];
        const uint32_t filenameOffset = filenameOffsets[filenameEntry];

        uint32_t fileIndex = 0;
        auto it = fileIndexByOffset.find(filenameOffset);
        if (it == fileIndexByOffset.end()) {
            fileIndex = static_cast<uint32_t>(files.size());
            files.push_back(namesStream.GetFilename(filenameOffset));
            fileIndexByOffset[filenameOffset] = fileIndex;
        } else {
            fileIndex = it->second;
        }

        const uint32_t rva = imageSectionStream.ConvertSectionOffsetToRVA(s.index, s.offset);
        lines.push_back({rva, lineNumber, fileIndex});
    }

    // 最终按 RVA 升序（section 顺序并不保证 RVA 顺序）
    std::sort(lines.begin(), lines.end(), [](const PdbResolver::LineEntry& a,
                                             const PdbResolver::LineEntry& b) {
        return a.rva < b.rva;
    });
}

}  // namespace

PdbResolver::PdbResolver(std::unordered_map<std::string, std::string> pdbMap,
                         std::string cacheDir,
                         bool useCache)
    : pdbMap_(std::move(pdbMap)), cacheDir_(std::move(cacheDir)), useCache_(useCache) {}

PdbResolver::~PdbResolver() = default;

std::string PdbResolver::FindPdb(const std::string& modPath) const {
    std::string base = std::filesystem::path(modPath).filename().string();
    std::string key = base;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });

    auto it = pdbMap_.find(key);
    if (it != pdbMap_.end() && std::filesystem::exists(it->second)) {
        return it->second;
    }

    // 同目录同名 .pdb 回退
    std::filesystem::path pdbPath = modPath;
    pdbPath.replace_extension(".pdb");
    if (std::filesystem::exists(pdbPath)) {
        return pdbPath.string();
    }
    return "";
}

const PdbResolver::PdbData* PdbResolver::Load(const std::string& pdbPath) {
    auto it = loaded_.find(pdbPath);
    if (it != loaded_.end()) {
        return &it->second;
    }

    PdbData data;

    // 1) 尝试从缓存加载
    if (useCache_) {
        std::filesystem::path p(pdbPath);
        std::string key = std::to_string(std::filesystem::file_size(p)) + "-" +
                          std::to_string(std::filesystem::last_write_time(p).time_since_epoch().count());
        std::filesystem::path cachePath = std::filesystem::path(cacheDir_) / (p.stem().string() + "-" + key + ".json");
        if (std::filesystem::exists(cachePath)) {
            try {
                std::ifstream in(cachePath);
                nlohmann::json j;
                in >> j;
                for (const auto& f : j.at("functions")) {
                    data.functions.push_back({f[0].get<uint32_t>(), f[1].get<std::string>()});
                }
                for (const auto& f : j.at("files")) {
                    data.files.push_back(f.get<std::string>());
                }
                for (const auto& l : j.at("lines")) {
                    data.lines.push_back({l[0].get<uint32_t>(), l[1].get<uint32_t>(), l[2].get<uint32_t>()});
                }
                LOG_INFO("PDB cache hit: {} ({} functions, {} lines)", pdbPath, data.functions.size(), data.lines.size());
                return &loaded_.emplace(pdbPath, std::move(data)).first->second;
            } catch (const std::exception& ex) {
                LOG_WARN("PDB cache corrupted ({}), re-parsing: {}", cachePath.string(), ex.what());
                data = PdbData{};
            }
        }
    }

    // 2) 解析 PDB
    LOG_INFO("Parsing PDB: {}", pdbPath);
    MappedFile file(pdbPath);
    if (!file.ok()) {
        LOG_ERROR("Cannot memory-map PDB file: {}", pdbPath);
        return &loaded_.emplace(pdbPath, PdbData{}).first->second;
    }

    if (!PdbOk(PDB::ValidateFile(file.data(), file.size()))) {
        LOG_ERROR("Invalid PDB file: {}", pdbPath);
        return &loaded_.emplace(pdbPath, PdbData{}).first->second;
    }

    const PDB::RawFile rawFile = PDB::CreateRawFile(file.data());
    if (!PdbOk(PDB::HasValidDBIStream(rawFile))) {
        LOG_ERROR("PDB has no valid DBI stream: {}", pdbPath);
        return &loaded_.emplace(pdbPath, PdbData{}).first->second;
    }

    const PDB::DBIStream dbiStream = PDB::CreateDBIStream(rawFile);
    if (!PdbOk(dbiStream.HasValidImageSectionStream(rawFile)) ||
        !PdbOk(dbiStream.HasValidSymbolRecordStream(rawFile)) ||
        !PdbOk(dbiStream.HasValidPublicSymbolStream(rawFile))) {
        LOG_ERROR("PDB is missing required sub-streams: {}", pdbPath);
        return &loaded_.emplace(pdbPath, PdbData{}).first->second;
    }

    const PDB::InfoStream infoStream(rawFile);
    if (infoStream.UsesDebugFastLink()) {
        LOG_ERROR("PDB was linked with /DEBUG:FASTLINK, which is not supported: {}", pdbPath);
        return &loaded_.emplace(pdbPath, PdbData{}).first->second;
    }

    // 收集函数符号 + 行信息
    std::vector<FuncSymbol> funcs = CollectFunctionSymbols(rawFile, dbiStream);
    data.functions.reserve(funcs.size());
    for (auto& f : funcs) {
        data.functions.push_back({f.rva, std::move(f.name)});
    }
    BuildLines(rawFile, dbiStream, infoStream, data.files, data.lines);

    LOG_INFO("PDB parsed: {} ({} functions, {} lines, {} files)", pdbPath,
             data.functions.size(), data.lines.size(), data.files.size());

    // 3) 写缓存
    if (useCache_) {
        try {
            std::filesystem::create_directories(cacheDir_);
            std::filesystem::path p(pdbPath);
            std::string key = std::to_string(std::filesystem::file_size(p)) + "-" +
                              std::to_string(std::filesystem::last_write_time(p).time_since_epoch().count());
            std::filesystem::path cachePath = std::filesystem::path(cacheDir_) / (p.stem().string() + "-" + key + ".json");

            nlohmann::json j;
            nlohmann::json funcsJson = nlohmann::json::array();
            for (const auto& f : data.functions) {
                funcsJson.push_back({f.rva, f.name});
            }
            nlohmann::json ls = nlohmann::json::array();
            for (const auto& l : data.lines) {
                ls.push_back({l.rva, l.line, l.fileIndex});
            }
            j["functions"] = funcsJson;
            j["files"] = data.files;
            j["lines"] = ls;
            std::ofstream out(cachePath);
            out << j.dump();
            LOG_DEBUG("PDB cache written: {}", cachePath.string());
        } catch (const std::exception& ex) {
            LOG_WARN("Failed to write PDB cache: {}", ex.what());
        }
    }

    return &loaded_.emplace(pdbPath, std::move(data)).first->second;
}

ResolvedLocation PdbResolver::Resolve(const std::string& modPath, uint32_t rva) {
    std::string base = std::filesystem::path(modPath).filename().string();
    std::string key = base + "#" + std::to_string(rva);

    auto hit = resolvedCache_.find(key);
    if (hit != resolvedCache_.end()) {
        return hit->second;
    }

    ResolvedLocation result;
    const std::string pdb = FindPdb(modPath);
    if (!pdb.empty()) {
        const PdbData* data = Load(pdb);
        if (data && (!data->functions.empty() || !data->lines.empty())) {
            // 二分查找：最后一个起始 RVA <= 目标 RVA 的函数
            if (!data->functions.empty()) {
                auto f = std::upper_bound(data->functions.begin(), data->functions.end(), rva,
                                          [](uint32_t v, const FuncEntry& e) { return v < e.rva; });
                if (f != data->functions.begin()) {
                    --f;
                    result.function = f->name;
                }
            }
            // 二分查找：最后一个起始 RVA <= 目标 RVA 的行
            if (!data->lines.empty()) {
                auto l = std::upper_bound(data->lines.begin(), data->lines.end(), rva,
                                          [](uint32_t v, const LineEntry& e) { return v < e.rva; });
                if (l != data->lines.begin()) {
                    --l;
                    if (l->fileIndex < data->files.size()) {
                        result.file = data->files[l->fileIndex];
                    }
                    result.line = l->line;
                }
            }
        } else if (!pdb.empty()) {
            LOG_DEBUG("No PDB data for module '{}' (RVA 0x{:x})", modPath, rva);
        }
    } else {
        LOG_DEBUG("No PDB found for module '{}'", modPath);
    }

    resolvedCache_[key] = result;
    return result;
}

}  // namespace hookfw
