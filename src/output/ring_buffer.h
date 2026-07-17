#pragma once
#include <cstdint>
#include <atomic>
#include <chrono>

namespace hookfw {

struct TraceRecord {
    uint32_t hookId;
    uint32_t threadId;
    double timestampUs;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint8_t phase;
    uint8_t padding[7];
};

class RingBuffer {
public:
    static constexpr size_t DEFAULT_CAPACITY = 10000000;
    static size_t globalCapacity;

    RingBuffer() : capacity_(globalCapacity), buffer_(new TraceRecord[globalCapacity]) {}
    ~RingBuffer() { delete[] buffer_; }

    static void setGlobalCapacity(size_t cap) { globalCapacity = cap; }
    size_t capacity() const { return capacity_; }

    bool push(const TraceRecord& record) {
        uint64_t pos = writePos_.fetch_add(1, std::memory_order_relaxed);
        buffer_[pos % capacity_] = record;
        commitPos_.fetch_add(1, std::memory_order_release);
        return true;
    }

    size_t drain(TraceRecord* out, size_t maxCount, uint64_t* outOverflow = nullptr) {
        uint64_t committed = commitPos_.load(std::memory_order_acquire);
        uint64_t read = readPos_;
        size_t available = static_cast<size_t>(committed - read);
        if (available == 0) return 0;
        if (available > maxCount) available = maxCount;
        if (available > capacity_) {
            if (outOverflow) *outOverflow = available - capacity_;
            readPos_ = committed - capacity_;
            read = readPos_;
            available = static_cast<size_t>(committed - read);
            if (available > maxCount) available = maxCount;
        } else if (outOverflow) {
            *outOverflow = 0;
        }
        for (size_t i = 0; i < available; i++) {
            out[i] = buffer_[(read + i) % capacity_];
        }
        readPos_ = read + available;
        return available;
    }

    uint64_t pending() const {
        uint64_t diff = commitPos_.load(std::memory_order_relaxed) - readPos_;
        return diff > capacity_ ? capacity_ : diff;
    }

    static double nowMicroseconds() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count() / 1000.0;
    }

private:
    size_t capacity_;
    TraceRecord* buffer_;
    std::atomic<uint64_t> writePos_{0};
    std::atomic<uint64_t> commitPos_{0};
    uint64_t readPos_ = 0;
};

inline size_t RingBuffer::globalCapacity = RingBuffer::DEFAULT_CAPACITY;

}
