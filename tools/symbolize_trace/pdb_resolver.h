#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace hookfw {

// RVA 解析结果：对应源码位置
struct ResolvedLocation {
    std::string function;   // 函数名（已去修饰），为空表示未能解析
    std::string file;       // 源码文件绝对路径，为空表示未能解析
    uint32_t line = 0;      // 源码行号，0 表示未知

    bool empty() const { return function.empty() && file.empty(); }
};

// 基于 raw_pdb 的 PDB 解析器：
// 一次性加载函数符号表与行信息表（均按 RVA 升序），随后通过二分查找定位任意 RVA 对应的代码位置。
// 支持磁盘缓存，避免每次都对同一个 PDB 重复解析。
class PdbResolver {
public:
    // pdbMap: 模块名(小写 basename) -> PDB 路径；cacheDir: 缓存目录；useCache: 是否启用缓存
    PdbResolver(std::unordered_map<std::string, std::string> pdbMap,
                std::string cacheDir,
                bool useCache = true);
    ~PdbResolver();

    PdbResolver(const PdbResolver&) = delete;
    PdbResolver& operator=(const PdbResolver&) = delete;

    // 根据模块路径(modPath)与 RVA 定位代码位置；内部完成模块->PDB 的映射与缓存命中
    ResolvedLocation Resolve(const std::string& modPath, uint32_t rva);

    // 内部数据结构（供 .cpp 中的解析辅助函数使用）
    struct FuncEntry {
        uint32_t rva;
        std::string name;
    };

    struct LineEntry {
        uint32_t rva;
        uint32_t line;
        uint32_t fileIndex;
    };

    // 单个 PDB 解析后的完整数据（均按 RVA 升序）
    struct PdbData {
        std::vector<FuncEntry> functions;
        std::vector<std::string> files;
        std::vector<LineEntry> lines;
    };

private:
    std::string FindPdb(const std::string& modPath) const;
    const PdbData* Load(const std::string& pdbPath);

    std::unordered_map<std::string, std::string> pdbMap_;
    std::string cacheDir_;
    bool useCache_;

    std::unordered_map<std::string, PdbData> loaded_;              // pdb 路径 -> 数据
    std::unordered_map<std::string, ResolvedLocation> resolvedCache_;  // "<basename>#<rva>" -> 结果
};

}  // namespace hookfw
