#pragma once
#include "config/perfetto_config.h"
#define PERFETTO_LOG_LEVEL 0 
#include "perfetto.h"
#include "output/track_event_observer.h"
#include <memory>
#include <string>
#include <cstdint>

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("SW").SetDescription("")
);

namespace hookfw {

class PerfettoTracer {
public:
    static PerfettoTracer& instance();

    bool initialize();
    void shutdown();
    bool isActive() const { return active_; }

private:
    PerfettoTracer() = default;
    ~PerfettoTracer();
    bool active_ = false;
    
    TrackEventObserver track_event_observer_;
};

}
