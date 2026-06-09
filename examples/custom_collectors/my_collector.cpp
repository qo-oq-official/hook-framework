#include "my_collector.h"
#include "logging/logger.h"
#include "output/file_output_writer.h"
#include <nlohmann/json.hpp>

namespace hookfw {

void MyAddCollector::onEntry(const HookContext& ctx) {
    if (!outputWriter_) {
        LOG_ERROR("MyAddCollector: no output writer");
        return;
    }

    try {
        int a = ctx.arg<int>(0);
        int b = ctx.arg<int>(1);

        LOG_INFO("MyAddCollector::onEntry: add_numbers({}, {})", a, b);

        nlohmann::json j;
        j["custom"] = true;
        j["function"] = "add_numbers";
        j["a"] = a;
        j["b"] = b;
        j["expected_result"] = a + b;

        auto* fileWriter = dynamic_cast<FileOutputWriter*>(outputWriter_.get());
        uint64_t seq = fileWriter ? fileWriter->nextSeq() : 0;
        outputWriter_->writeJson(moduleName_, functionName_, seq, "entry", j);
    } catch (const std::exception& ex) {
        LOG_ERROR("MyAddCollector::onEntry exception: {}", ex.what());
    }
}

void MyAddCollector::onExit(const HookContext& ctx) {
    if (!outputWriter_) {
        LOG_ERROR("MyAddCollector: no output writer");
        return;
    }

    try {
        int result = ctx.returnValue<int>();

        LOG_INFO("MyAddCollector::onExit: result={}", result);

        nlohmann::json j;
        j["custom"] = true;
        j["function"] = "add_numbers";
        j["result"] = result;

        auto* fileWriter = dynamic_cast<FileOutputWriter*>(outputWriter_.get());
        uint64_t seq = fileWriter ? fileWriter->nextSeq() : 0;
        outputWriter_->writeJson(moduleName_, functionName_, seq, "exit", j);
    } catch (const std::exception& ex) {
        LOG_ERROR("MyAddCollector::onExit exception: {}", ex.what());
    }
}

REGISTER_COLLECTOR("my_add_collector", MyAddCollector);

}
