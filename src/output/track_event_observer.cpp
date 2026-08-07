#include "output/track_event_observer.h"
#include "logging/logger.h"

// 当带有 track_event 的 Trace 任务被配置/设置时触发
void TrackEventObserver::OnSetup(const perfetto::DataSourceBase::SetupArgs& args) {
    LOG_DEBUG("TrackEvent OnSetup...");
}

// 当 Trace 真正开始录制时触发
void TrackEventObserver::OnStart(const perfetto::DataSourceBase::StartArgs&) {
    LOG_DEBUG("TrackEvent OnStart...");
    {
        std::lock_guard<std::mutex> lk(mtx_);
        ready_ = true;
    }
    cv_.notify_one();
}

// 当 Trace 录制停止时触发
void TrackEventObserver::OnStop(const perfetto::DataSourceBase::StopArgs&) {
    LOG_DEBUG("TrackEvent OnStop...");
}


