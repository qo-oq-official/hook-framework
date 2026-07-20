#include "output/file_output_writer.h"
#include "logging/logger.h"
#include <fstream>

namespace hookfw {

FileOutputWriter::FileOutputWriter(const std::string& baseDir)
    : pathBuilder_(baseDir) {
    LOG_INFO("FileOutputWriter created, baseDir={}", baseDir);
}

bool FileOutputWriter::writeJson(const std::string& moduleName,
                                 const std::string& functionName,
                                 uint64_t seq,
                                 const std::string& phase,
                                 const nlohmann::json& data) {
    try {
        auto path = pathBuilder_.buildJsonPath(moduleName, functionName, seq, phase);
        if (!pathBuilder_.ensureDirectory(path.parent_path())) {
            return false;
        }

        std::ofstream file(path);
        if (!file.is_open()) {
            LOG_ERROR("Cannot open file for writing: {}", path.string());
            return false;
        }

        file << data.dump(2);
        if (!file.good()) {
            LOG_ERROR("Write failed: {}", path.string());
            return false;
        }

        LOG_TRACE("JSON output written: {}", path.string());
        return true;
    } catch (const std::exception& ex) {
        LOG_ERROR("Exception in writeJson: {}", ex.what());
        return false;
    }
}

uint64_t FileOutputWriter::nextSeq() {
    return ++seqCounter_;
}

}
