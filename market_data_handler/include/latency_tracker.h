#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <array>


class LatencyTracker {
public:
    struct LatencyStats {
        uint64_t min_ns{0}, max_ns{0}, mean_ns{0};
        uint64_t p50_ns{0}, p95_ns{0}, p99_ns{0}, p999_ns{0};
        uint64_t sample_count{0};
    };

    static constexpr int    NUM_BUCKETS   = 64;   
    static constexpr size_t RING_CAPACITY = 1 << 20; 

    LatencyTracker() {
        for (auto& b : buckets_) b.store(0, std::memory_order_relaxed);
        min_.store(UINT64_MAX, std::memory_order_relaxed);
        max_.store(0,          std::memory_order_relaxed);
        sum_.store(0,          std::memory_order_relaxed);
        count_.store(0,        std::memory_order_relaxed);
    }

    void record(uint64_t latency_ns) noexcept {
        int bucket = bucket_for(latency_ns);
        buckets_[bucket].fetch_add(1, std::memory_order_relaxed);

        uint64_t old_min = min_.load(std::memory_order_relaxed);
        while (latency_ns < old_min &&
               !min_.compare_exchange_weak(old_min, latency_ns,
                   std::memory_order_relaxed)) {}

        uint64_t old_max = max_.load(std::memory_order_relaxed);
        while (latency_ns > old_max &&
               !max_.compare_exchange_weak(old_max, latency_ns,
                   std::memory_order_relaxed)) {}

        sum_.fetch_add(latency_ns, std::memory_order_relaxed);
        count_.fetch_add(1,        std::memory_order_relaxed);
    }

    LatencyStats get_stats() const noexcept {
        LatencyStats st;
        st.sample_count = count_.load(std::memory_order_relaxed);
        if (st.sample_count == 0) return st;
        st.min_ns  = min_.load(std::memory_order_relaxed);
        st.max_ns  = max_.load(std::memory_order_relaxed);
        st.mean_ns = sum_.load(std::memory_order_relaxed) / st.sample_count;

        uint64_t p50_target  = st.sample_count / 2;
        uint64_t p95_target  = st.sample_count * 95 / 100;
        uint64_t p99_target  = st.sample_count * 99 / 100;
        uint64_t p999_target = st.sample_count * 999 / 1000;

        uint64_t cumul = 0;
        bool p50_done=false, p95_done=false, p99_done=false, p999_done=false;
        for (int i = 0; i < NUM_BUCKETS; ++i) {
            cumul += buckets_[i].load(std::memory_order_relaxed);
            uint64_t bucket_upper = bucket_upper_ns(i);
            if (!p50_done  && cumul >= p50_target)  { st.p50_ns  = bucket_upper; p50_done  = true; }
            if (!p95_done  && cumul >= p95_target)  { st.p95_ns  = bucket_upper; p95_done  = true; }
            if (!p99_done  && cumul >= p99_target)  { st.p99_ns  = bucket_upper; p99_done  = true; }
            if (!p999_done && cumul >= p999_target) { st.p999_ns = bucket_upper; p999_done = true; break; }
        }
        return st;
    }

    void reset() noexcept {
        for (auto& b : buckets_) b.store(0, std::memory_order_relaxed);
        min_.store(UINT64_MAX, std::memory_order_relaxed);
        max_.store(0,          std::memory_order_relaxed);
        sum_.store(0,          std::memory_order_relaxed);
        count_.store(0,        std::memory_order_relaxed);
    }

    uint64_t raw_bucket(int i) const noexcept {
        return buckets_[i].load(std::memory_order_relaxed);
    }

    void export_csv(const std::string& path) const {
        std::ofstream f(path);
        f << "bucket_lower_ns,bucket_upper_ns,count\n";
        for (int i = 0; i < NUM_BUCKETS; ++i) {
            uint64_t lo = (i == 0) ? 0 : (uint64_t(1) << (i-1));
            uint64_t hi = bucket_upper_ns(i);
            f << lo << ',' << hi << ',' 
              << buckets_[i].load(std::memory_order_relaxed) << '\n';
        }
    }

private:
    std::array<std::atomic<uint64_t>, NUM_BUCKETS> buckets_;
    std::atomic<uint64_t> min_, max_, sum_, count_;

    static int bucket_for(uint64_t ns) noexcept {
        if (ns == 0) return 0;
        int b = 63 - __builtin_clzll(ns);
        return (b < NUM_BUCKETS) ? b : (NUM_BUCKETS - 1);
    }
    static uint64_t bucket_upper_ns(int i) noexcept {
        return (i < 63) ? (uint64_t(1) << i) : UINT64_MAX;
    }
};
