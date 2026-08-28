#include "pdb_resolver.h"

#include "logger.h"

#include <windows.h>
#include <DbgHelp.h>

#include <PDB.h>
#include <PDB_RawFile.h>
#include <PDB_InfoStream.h>
#include <PDB_DBIStream.h>
#include <PDB_ModuleInfoStream.h>
#include <PDB_ModuleSymbolStream.h>
#include <PDB_ModuleLineStream.h>
#include <PDB_NamesStream.h>
#include <PDB_PublicSymbolStream.h>
#include <PDB_ImageSectionStream.h>
#include <PDB_CoalescedMSFStream.h>
#include "Foundation/PDB_PointerUtil.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_set>

#pragma comment(lib, "dbghelp.lib")

namespace hookfw {

namespace {

// 内存映射文件（RAII）：用于只读映射 PDB 文件
class MappedFile {
public:
    explicit MappedFile(const std::string& path) {
        file_ = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) {
            return;
        }
        LARGE_INTEGER size;
        if (!GetFileSizeEx(file_, &size)) {
            CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
            return;
        }
        size_ = static_cast<size_t>(size.QuadPart);

        mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping_) {
            CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
            return;
        }
        data_ = MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0);
        if (!data_) {
            CloseHandle(mapping_);
            CloseHandle(file_);
            mapping_ = nullptr;
            file_ = INVALID_HANDLE_VALUE;
        }
    }

    ~MappedFile() {
        if (data_) {
            UnmapViewOfFile(data_);
        }
        if (mapping_) {
            CloseHandle(mapping_);
        }
        if (file_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_);
        }
    }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    bool ok() const { return data_ != nullptr; }
    const void* data() const { return data_; }
    size_t size() const { return size_; }

private:
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
    void* data_ = nullptr;
    size_t size_ = 0;
};

// 将 MSVC 修饰名（以 '?' 开头）还原为可读签名，其余原样返回
std::string undecorate(const std::string& name) {
    if (name.empty() || name[0] != '?') {
        return name;
    }
    char buf[2048] = {};
    DWORD flags = UNDNAME_NO_ACCESS_SPECIFIERS | UNDNAME_NO_MEMBER_TYPE |
                  UNDNAME_NO_MS_KEYWORDS | UNDNAME_NO_RETURN_UDT_MODEL |
                  UNDNAME_NO_THISTYPE | UNDNAME_NO_ALLOCATION_MODEL;
    if (UnDecorateSymbolName(name.c_str(), buf, sizeof(buf), flags) > 0) {
        return std::string(buf);
    }
    return name;
}

// 检查 raw_pdb 的 ErrorCode 是否表示成功
bool ok(PDB::ErrorCode code) {
    return code == PDB::ErrorCode::Success;
}

// 从模块符号流与公共符号流中收集所有函数符号（函数名 + 起始 RVA），结果按 RVA 升序。
// 逻辑移植自 raw_pdb 的 ExampleFunctionSymbols.cpp。
void BuildFunctions(const PDB::RawFile& rawFile, const PDB::DBIStream& dbiStream,
                    std::vector<PdbResolver::FuncEntry>& functions) {
    using namespace PDB::CodeView::DBI;

    const PDB::ImageSectionStream imageSectionStream = dbiStream.CreateImageSectionStream(rawFile);
    const PDB::ModuleInfoStream moduleInfoStream = dbiStream.CreateModuleInfoStream(rawFile);
    const PDB::CoalescedMSFStream symbolRecordStream = dbiStream.CreateSymbolRecordStream(rawFile);

    std::unordered_set<uint32_t> seenRvas;

    // 1) 从各模块符号流收集函数（通常能拿到约 90% 的函数）
    for (const PDB::ModuleInfoStream::Module& module : moduleInfoStream.GetModules()) {
        if (!module.HasSymbolStream()) {
            continue;
        }
        const PDB::ModuleSymbolStream moduleSymbolStream = module.CreateSymbolStream(rawFile);
        moduleSymbolStream.ForEachSymbol([&](const PDB::CodeView::DBI::Record* record) {
            const char* name = nullptr;
            uint32_t rva = 0u;

            switch (record->header.kind) {
                case SymbolRecordKind::S_LPROC32:
                    name = record->data.S_LPROC32.name;
                    rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LPROC32.section,
                                                                       record->data.S_LPROC32.offset);
                    break;
                case SymbolRecordKind::S_GPROC32:
                    name = record->data.S_GPROC32.name;
                    rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GPROC32.section,
                                                                       record->data.S_GPROC32.offset);
                    break;
                case SymbolRecordKind::S_LPROC32_ID:
                    name = record->data.S_LPROC32_ID.name;
                    rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LPROC32_ID.section,
                                                                       record->data.S_LPROC32_ID.offset);
                    break;
                case SymbolRecordKind::S_GPROC32_ID:
                    name = record->data.S_GPROC32_ID.name;
                    rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GPROC32_ID.section,
                                                                       record->data.S_GPROC32_ID.offset);
                    break;
                case SymbolRecordKind::S_THUNK32:
                    if (record->data.S_THUNK32.thunk == ThunkOrdinal::TrampolineIncremental) {
                        name = "ILT";
                        rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_THUNK32.section,
                                                                           record->data.S_THUNK32.offset);
                    }
                    break;
                case SymbolRecordKind::S_TRAMPOLINE:
                    name = "ILT";
                    rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_TRAMPOLINE.thunkSection,
                                                                       record->data.S_TRAMPOLINE.thunkOffset);
                    break;
                default:
                    return;
            }

            if (rva == 0u) {
                return;
            }
            if (seenRvas.insert(rva).second) {
                functions.push_back({rva, undecorate(name ? name : "")});
            }
        });
    }

    // 2) 从公共符号流补充模块流中缺失的函数符号
    const PDB::PublicSymbolStream publicSymbolStream = dbiStream.CreatePublicSymbolStream(rawFile);
    for (const PDB::HashRecord& hashRecord : publicSymbolStream.GetRecords()) {
        const PDB::CodeView::DBI::Record* record = publicSymbolStream.GetRecord(symbolRecordStream, hashRecord);
        if (record->header.kind != SymbolRecordKind::S_PUB32) {
            continue;
        }
        const uint32_t flags = static_cast<uint32_t>(record->data.S_PUB32.flags);
        const uint32_t functionFlag = static_cast<uint32_t>(PublicSymbolFlags::Function);
        if ((flags & functionFlag) == 0u) {
            continue;  // 只关心函数
        }
        const uint32_t rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_PUB32.section,
                                                                          record->data.S_PUB32.offset);
        if (rva == 0u || seenRvas.count(rva)) {
            continue;
        }
        functions.push_back({rva, undecorate(record->data.S_PUB32.name)});
    }

    std::sort(functions.begin(), functions.end(), [](const PdbResolver::FuncEntry& a,
                                                     const PdbResolver::FuncEntry& b) {
        return a.rva < b.rva;
    });
}

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

    if (!ok(PDB::ValidateFile(file.data(), file.size()))) {
        LOG_ERROR("Invalid PDB file: {}", pdbPath);
        return &loaded_.emplace(pdbPath, PdbData{}).first->second;
    }

    const PDB::RawFile rawFile = PDB::CreateRawFile(file.data());
    if (!ok(PDB::HasValidDBIStream(rawFile))) {
        LOG_ERROR("PDB has no valid DBI stream: {}", pdbPath);
        return &loaded_.emplace(pdbPath, PdbData{}).first->second;
    }

    const PDB::DBIStream dbiStream = PDB::CreateDBIStream(rawFile);
    if (!ok(dbiStream.HasValidImageSectionStream(rawFile)) ||
        !ok(dbiStream.HasValidSymbolRecordStream(rawFile)) ||
        !ok(dbiStream.HasValidPublicSymbolStream(rawFile))) {
        LOG_ERROR("PDB is missing required sub-streams: {}", pdbPath);
        return &loaded_.emplace(pdbPath, PdbData{}).first->second;
    }

    const PDB::InfoStream infoStream(rawFile);
    if (infoStream.UsesDebugFastLink()) {
        LOG_ERROR("PDB was linked with /DEBUG:FASTLINK, which is not supported: {}", pdbPath);
        return &loaded_.emplace(pdbPath, PdbData{}).first->second;
    }

    BuildFunctions(rawFile, dbiStream, data.functions);
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
            nlohmann::json funcs = nlohmann::json::array();
            for (const auto& f : data.functions) {
                funcs.push_back({f.rva, f.name});
            }
            nlohmann::json ls = nlohmann::json::array();
            for (const auto& l : data.lines) {
                ls.push_back({l.rva, l.line, l.fileIndex});
            }
            j["functions"] = funcs;
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
