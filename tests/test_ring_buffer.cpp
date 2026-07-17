#include <hook_framework/hook_framework.h>
#include "../src/output/ring_buffer.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <set>

static int g_ok = 0, g_bad = 0;
#define CHECK(cond, msg) do { if (cond) { std::cout << "  [PASS] " << msg << "\n"; g_ok++; } else { std::cout << "  [FAIL] " << msg << "\n"; g_bad++; } } while(0)

using namespace hookfw;

// ── Test 1: Basic single-thread push/drain ────────────────────────────

void test_basic() {
    std::cout << "\n[1] Basic push/drain ...\n";

    RingBuffer::setGlobalCapacity(100);
    RingBuffer buf;

    for (uint32_t i = 0; i < 50; i++) {
        TraceRecord rec{};
        rec.hookId = i;
        buf.push(rec);
    }

    uint64_t overflow = 999;
    TraceRecord batch[100];
    size_t n = buf.drain(batch, 100, &overflow);

    CHECK(n == 50, "drain returned 50");
    CHECK(overflow == 0, "overflow == 0 (no excess)");
    CHECK(buf.pending() == 0, "pending() == 0 after drain");

    // Verify records in order
    bool inorder = true;
    for (size_t i = 0; i < n && inorder; i++) {
        if (batch[i].hookId != i) inorder = false;
    }
    CHECK(inorder, "records in order");
}

// ── Test 2: Over-capacity overflow ────────────────────────────────────

void test_overflow() {
    std::cout << "\n[2] Over-capacity overflow ...\n";

    RingBuffer::setGlobalCapacity(10);
    RingBuffer buf;

    // Push 30 records (capacity=10 → first 20 will be overwritten)
    for (uint32_t i = 0; i < 30; i++) {
        TraceRecord rec{};
        rec.hookId = i;
        buf.push(rec);
    }

    uint64_t overflow = 0;
    TraceRecord batch[30];
    size_t n = buf.drain(batch, 30, &overflow);

    // First 20 overwritten → we should see records 20-29 (10 records)
    CHECK(n == 10, "drain returned 10 after overflow (got " + std::to_string(n) + ")");
    CHECK(overflow > 0, "overflow detected (got " + std::to_string(overflow) + ")");
    CHECK(buf.pending() == 0, "pending() == 0");

    // Verify we got the LAST 10 records (hookId 20-29)
    size_t correct = 0;
    for (size_t i = 0; i < n; i++) {
        if (batch[i].hookId >= 20 && batch[i].hookId < 30) correct++;
    }
    CHECK(correct == 10, "all 10 records are from last 10 pushes");
}

// ── Test 3: Multi-threaded concurrent push ────────────────────────────

void test_mt_push() {
    std::cout << "\n[3] Multi-threaded concurrent push ...\n";

    constexpr size_t CAP = 10000;
    constexpr size_t PER_THREAD = 2000;
    constexpr int THREADS = 4;

    RingBuffer::setGlobalCapacity(CAP);
    RingBuffer buf;

    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; t++) {
        threads.emplace_back([&buf, &start, t]() {
            while (!start.load()) { /* spin */ }
            for (size_t i = 0; i < PER_THREAD; i++) {
                TraceRecord rec{};
                rec.hookId = static_cast<uint32_t>(t * PER_THREAD + i);
                buf.push(rec);
            }
        });
    }

    start.store(true);
    for (auto& t : threads) t.join();

    // Drain
    TraceRecord batch[THREADS * PER_THREAD + 100];
    size_t n = buf.drain(batch, sizeof(batch)/sizeof(batch[0]));

    CHECK(n == THREADS * PER_THREAD, "drain returned all " + std::to_string(THREADS * PER_THREAD) + " records (got " + std::to_string(n) + ")");

    // Verify no duplicates, all IDs 0..(THREADS*PER_THREAD-1) present
    std::set<uint32_t> seen;
    for (size_t i = 0; i < n; i++) seen.insert(batch[i].hookId);
    CHECK(seen.size() == THREADS * PER_THREAD, "all " + std::to_string(THREADS * PER_THREAD) + " IDs unique");
    bool range = *seen.begin() == 0 && *seen.rbegin() == THREADS * PER_THREAD - 1;
    CHECK(range, "ID range complete");
}

// ── Test 4: Concurrent push + drain ────────────────────────────────────

void test_mt_push_drain() {
    std::cout << "\n[4] Concurrent push + drain ...\n";

    RingBuffer::setGlobalCapacity(50000);
    RingBuffer buf;

    std::atomic<bool> stop{false};
    std::atomic<uint64_t> totalPushed{0};
    std::atomic<uint64_t> totalDrained{0};

    // 2 producer threads
    std::vector<std::thread> producers;
    for (int t = 0; t < 2; t++) {
        producers.emplace_back([&]() {
            size_t local = 0;
            while (!stop.load()) {
                TraceRecord rec{};
                rec.hookId = static_cast<uint32_t>(local++);
                buf.push(rec);
                totalPushed.fetch_add(1);
                if (local % 1000 == 0) std::this_thread::yield();
            }
        });
    }

    // 1 consumer thread
    std::thread consumer([&]() {
        TraceRecord batch[1024];
        while (!stop.load()) {
            size_t n = buf.drain(batch, 1024);
            totalDrained.fetch_add(n);
            std::this_thread::yield();
        }
        // Final drain
        size_t n;
        while ((n = buf.drain(batch, 1024)) > 0)
            totalDrained.fetch_add(n);
    });

    // Run for 1 second
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop.store(true);

    for (auto& t : producers) t.join();
    consumer.join();

    uint64_t pushed = totalPushed.load();
    uint64_t drained = totalDrained.load();
    std::cout << "    pushed=" << pushed << " drained=" << drained << "\n";

    CHECK(pushed > 10000, "significant push volume (got " + std::to_string(pushed) + ")");
    CHECK(drained > 0, "drain consumed records");
    CHECK(buf.pending() == 0, "buffer drained to 0");
}

// ── Test 5: Circular wraparound ────────────────────────────────────────

void test_wraparound() {
    std::cout << "\n[5] Circular wraparound ...\n";

    RingBuffer::setGlobalCapacity(16);
    RingBuffer buf;

    // Fill with 0..15
    for (uint32_t i = 0; i < 16; i++) {
        TraceRecord rec{}; rec.hookId = i;
        buf.push(rec);
    }

    // Drain 8
    TraceRecord batch[20];
    size_t n = buf.drain(batch, 8);
    CHECK(n == 8, "drained first 8");

    // Push 8 more (16..23) — should wrap around
    for (uint32_t i = 16; i < 24; i++) {
        TraceRecord rec{}; rec.hookId = i;
        buf.push(rec);
    }

    // Drain remaining 16 (8 original + 8 new)
    n = buf.drain(batch, 20);
    CHECK(n == 16, "drained remaining 16");
    bool hasOld = false, hasNew = false;
    for (size_t i = 0; i < n; i++) {
        if (batch[i].hookId >= 8 && batch[i].hookId < 16) hasOld = true;
        if (batch[i].hookId >= 16 && batch[i].hookId < 24) hasNew = true;
    }
    CHECK(hasOld, "contains old records (8-15)");
    CHECK(hasNew, "contains new records (16-23)");
}

// ── Test 6: pending() accuracy ────────────────────────────────────────

void test_pending() {
    std::cout << "\n[6] pending() accuracy ...\n";

    RingBuffer::setGlobalCapacity(100);
    RingBuffer buf;

    CHECK(buf.pending() == 0, "pending=0 initially");

    for (uint32_t i = 0; i < 30; i++) {
        TraceRecord rec{}; rec.hookId = i;
        buf.push(rec);
    }
    CHECK(buf.pending() == 30, "pending=30 after 30 pushes");

    TraceRecord batch[10];
    buf.drain(batch, 10);
    CHECK(buf.pending() == 20, "pending=20 after drain 10");

    // Push 85 more → total=105 > capacity=100 → overflow
    for (uint32_t i = 0; i < 85; i++) {
        TraceRecord rec{}; rec.hookId = 100 + i;
        buf.push(rec);
    }
    uint64_t pending = buf.pending();
    CHECK(pending <= 100, "pending <= capacity after overflow (got " + std::to_string(pending) + ")");
}

// ── main ───────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Ring Buffer Tests ===\n";

    test_basic();
    test_overflow();
    test_mt_push();
    test_mt_push_drain();
    test_wraparound();
    test_pending();

    std::cout << "\n=== Results: " << g_ok << " passed, " << g_bad << " failed ===\n";
    return g_bad > 0 ? 1 : 0;
}
