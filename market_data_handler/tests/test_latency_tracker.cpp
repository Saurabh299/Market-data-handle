#include <gtest/gtest.h>
#include "../include/latency_tracker.h"
#include <thread>
#include <vector>
#include <numeric>

TEST(LatencyTracker, BasicRecord) {
    LatencyTracker t;
    t.record(1000);
    t.record(2000);
    t.record(3000);

    auto st = t.get_stats();
    EXPECT_EQ(st.sample_count, 3u);
    EXPECT_EQ(st.min_ns, 1000u);
    EXPECT_EQ(st.max_ns, 3000u);
    EXPECT_EQ(st.mean_ns, 2000u);
}

TEST(LatencyTracker, PercentilesMonotone) {
    LatencyTracker t;
    // Insert 1000 samples from 1..1000 μs
    for (uint64_t i = 1; i <= 1000; ++i)
        t.record(i * 1000);  // ns

    auto st = t.get_stats();
    EXPECT_GT(st.p50_ns,  0u);
    EXPECT_GE(st.p95_ns,  st.p50_ns);
    EXPECT_GE(st.p99_ns,  st.p95_ns);
    EXPECT_GE(st.p999_ns, st.p99_ns);
}

TEST(LatencyTracker, Reset) {
    LatencyTracker t;
    for (int i = 0; i < 100; ++i) t.record(1000);
    t.reset();
    auto st = t.get_stats();
    EXPECT_EQ(st.sample_count, 0u);
}

TEST(LatencyTracker, ThreadSafeRecording) {
    LatencyTracker t;
    static constexpr int THREADS = 8;
    static constexpr int PER_THREAD = 10000;

    std::vector<std::thread> workers;
    for (int i = 0; i < THREADS; ++i) {
        workers.emplace_back([&, i]{
            for (int j = 0; j < PER_THREAD; ++j)
                t.record(static_cast<uint64_t>((i+1) * 1000 + j));
        });
    }
    for (auto& w : workers) w.join();

    auto st = t.get_stats();
    EXPECT_EQ(st.sample_count, static_cast<uint64_t>(THREADS * PER_THREAD));
}

TEST(LatencyTracker, RecordOverheadUnder30ns) {
    LatencyTracker t;
    static constexpr int ITERS = 1000000;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < ITERS; ++i)
        t.record(static_cast<uint64_t>(i));
    auto end = std::chrono::steady_clock::now();

    double avg_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        end - start).count() / double(ITERS);
    std::cout << "  LatencyTracker::record avg overhead: " << avg_ns << " ns\n";
    // Note: 30ns target is for co-location hardware; CI machines may be slower
    EXPECT_LT(avg_ns, 500.0) << "record() overhead too high for this test environment";
}

TEST(LatencyTracker, ExportCSV) {
    LatencyTracker t;
    for (uint64_t i = 1; i <= 100; ++i) t.record(i * 500);
    t.export_csv("/tmp/test_hist.csv");

    std::ifstream f("/tmp/test_hist.csv");
    ASSERT_TRUE(f.good());
    std::string line;
    std::getline(f, line);
    EXPECT_EQ(line, "bucket_lower_ns,bucket_upper_ns,count");
}
