#pragma once
#include <cstdint>
#include <atomic>
#include <chrono>

namespace hookfw {

struct TraceRecord {
    uint32_t hookId;
    uint32_t threadId;
    int64_t timestampUs;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint8_t phase;
    uint8_t padding[7];
};

class RingBuffer {
public:
    static constexpr size_t CAPACITY = 1024 * 1024;

    RingBuffer() : buffer_(new TraceRecord[CAPACITY]) {}
    ~RingBuffer() { delete[] buffer_; }

    bool push(const TraceRecord& record) {
        uint64_t pos = writePos_.fetch_add(1, std::memory_order_relaxed);
        buffer_[pos % CAPACITY] = record;
        commitPos_.fetch_add(1, std::memory_order_release);
        return true;
    }

    size_t drain(TraceRecord* out, size_t maxCount) {
        uint64_t committed = commitPos_.load(std::memory_order_acquire);
        uint64_t read = readPos_;
        size_t available = static_cast<size_t>(committed - read);
        if (available == 0) return 0;
        if (available > maxCount) available = maxCount;
        if (available > CAPACITY) {
            readPos_ = committed - CAPACITY;
            read = readPos_;
            available = static_cast<size_t>(committed - read);
            if (available > maxCount) available = maxCount;
        }
        for (size_t i = 0; i < available; i++) {
            out[i] = buffer_[(read + i) % CAPACITY];
        }
        readPos_ = read + available;
        return available;
    }

    uint64_t pending() const {
        return commitPos_.load(std::memory_order_relaxed) - readPos_;
    }

    static int64_t nowMicroseconds() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
    }

private:
    TraceRecord* buffer_;
    std::atomic<uint64_t> writePos_{0};
    std::atomic<uint64_t> commitPos_{0};
    uint64_t readPos_ = 0;
};

}
