#include <mutex>
#include <condition_variable>
#include <chrono>
#include "perfetto.h"
class TrackEventObserver : public perfetto::TrackEventSessionObserver {
public:
    // 当带有 track_event 的 Trace 任务被配置/设置时触发
    void OnSetup(const perfetto::DataSourceBase::SetupArgs& args) override;

    // 当 Trace 真正开始录制时触发
    void OnStart(const perfetto::DataSourceBase::StartArgs&) override;

    // 当 Trace 录制停止时触发
    void OnStop(const perfetto::DataSourceBase::StopArgs&) override;


    template<typename Rep, typename Period>
    bool WaitReady(const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lk(mtx_);
        return cv_.wait_for(lk, timeout, [this]() {
            return ready_;
        });
    }
private:
    std::mutex mtx_;
    std::condition_variable cv_;
    bool ready_ = false;
};