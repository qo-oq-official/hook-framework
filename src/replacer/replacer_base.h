#pragma once
#include <string>

namespace hookfw {

class ReplacerBase {
public:
    virtual ~ReplacerBase() = default;
    virtual void* getReplacementAddress() = 0;
    virtual std::string description() const = 0;
};

}
