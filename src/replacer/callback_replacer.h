#pragma once
#include "replacer/replacer_base.h"

namespace hookfw {

class CallbackReplacer : public ReplacerBase {
public:
    explicit CallbackReplacer(void* funcPtr);
    ~CallbackReplacer() override = default;

    void* getReplacementAddress() override;
    std::string description() const override;

private:
    void* funcPtr_;
};

}
