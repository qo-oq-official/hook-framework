#pragma once

// 供 symbolize_trace / generate_hooks 共用的 PDB 基础工具：
//   - MappedFile：只读内存映射 PDB 文件（RAII）
//   - undecorate：MSVC 修饰名还原为可读签名
//   - CollectFunctionSymbols：从模块符号流 + 公共符号流收集函数符号（名字 + 起始 RVA）

#include <windows.h>
#include <DbgHelp.h>

#include <PDB.h>
#include <PDB_RawFile.h>
#include <PDB_DBIStream.h>
#include <PDB_ModuleInfoStream.h>
#include <PDB_ModuleSymbolStream.h>
#include <PDB_PublicSymbolStream.h>
#include <PDB_ImageSectionStream.h>
#include <PDB_CoalescedMSFStream.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace hookfw {

// 函数符号：起始 RVA + 去修饰后的名字
struct FuncSymbol {
    uint32_t rva;
    std::string name;
};

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
inline std::string undecorate(const std::string& name) {
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
inline bool PdbOk(PDB::ErrorCode code) {
    return code == PDB::ErrorCode::Success;
}

// 从模块符号流与公共符号流中收集所有函数符号（函数名 + 起始 RVA），结果按 RVA 升序。
// 逻辑移植自 raw_pdb 的 ExampleFunctionSymbols.cpp。
inline std::vector<FuncSymbol> CollectFunctionSymbols(const PDB::RawFile& rawFile,
                                                      const PDB::DBIStream& dbiStream) {
    using namespace PDB::CodeView::DBI;

    const PDB::ImageSectionStream imageSectionStream = dbiStream.CreateImageSectionStream(rawFile);
    const PDB::ModuleInfoStream moduleInfoStream = dbiStream.CreateModuleInfoStream(rawFile);
    const PDB::CoalescedMSFStream symbolRecordStream = dbiStream.CreateSymbolRecordStream(rawFile);

    std::vector<FuncSymbol> functions;
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

    std::sort(functions.begin(), functions.end(), [](const FuncSymbol& a, const FuncSymbol& b) {
        return a.rva < b.rva;
    });
    return functions;
}

}  // namespace hookfw
