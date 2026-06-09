#include "replacer/callback_replacer.h"
#include "logging/logger.h"
#include <format>

namespace hookfw {

CallbackReplacer::CallbackReplacer(void* funcPtr) : funcPtr_(funcPtr) {
    LOG_INFO("CallbackReplacer created with func at {:#x}",
             reinterpret_cast<uintptr_t>(funcPtr));
}

void* CallbackReplacer::getReplacementAddress() {
    return funcPtr_;
}

std::string CallbackReplacer::description() const {
    return std::format("callback@{:#x}", reinterpret_cast<uintptr_t>(funcPtr_));
}

}
