#pragma once
#include "collector/collector_base.h"

namespace hookfw {

// Collector that captures COM interface creation via CoCreateInstance/QueryInterface.
// On exit, reads the IID from riid param and interface pointer from ppv,
// checks ComRegistry for matching targets, and patches the vtable.
class ComInterfaceCaptureCollector : public CollectorBase {
public:
    void onEntry(const HookContext& ctx) override;
    void onExit(const HookContext& ctx) override;
};

}  // namespace hookfw
