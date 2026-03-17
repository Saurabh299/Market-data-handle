

#include "../../include/latency_tracker.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <algorithm>

void print_latency_histogram(const LatencyTracker& tracker,
                              const std::string& title,
                              uint64_t max_bar_width)
{
    auto st = tracker.get_stats();
    if (st.sample_count == 0) {
        std::cout << title << ": no samples\n";
        return;
    }

    std::cout << "\n=== " << title << " ===\n"
              << "  Samples : " << st.sample_count << "\n"
              << "  Min     : " << st.min_ns   << " ns\n"
              << "  Mean    : " << st.mean_ns  << " ns\n"
              << "  p50     : " << st.p50_ns   << " ns\n"
              << "  p95     : " << st.p95_ns   << " ns\n"
              << "  p99     : " << st.p99_ns   << " ns\n"
              << "  p999    : " << st.p999_ns  << " ns\n"
              << "  Max     : " << st.max_ns   << " ns\n\n";

    uint64_t peak = 0;
    for (int i = 0; i < LatencyTracker::NUM_BUCKETS; ++i) {
        uint64_t cnt = tracker.raw_bucket(i);
        if (cnt > peak) peak = cnt;
    }
    if (peak == 0) return;

    std::cout << std::left;
    for (int i = 0; i < LatencyTracker::NUM_BUCKETS; ++i) {
        uint64_t cnt = tracker.raw_bucket(i);
        if (cnt == 0) continue;

        uint64_t lo = (i == 0) ? 0 : (uint64_t(1) << (i - 1));
        uint64_t hi = (i < 63) ? (uint64_t(1) << i) : UINT64_MAX;

        // Format range label
        auto fmt_ns = [](uint64_t ns) -> std::string {
            if (ns == UINT64_MAX)  return "  ∞    ";
            if (ns >= 1000000000) return std::to_string(ns/1000000000) + "s   ";
            if (ns >= 1000000)    return std::to_string(ns/1000000)    + "ms  ";
            if (ns >= 1000)       return std::to_string(ns/1000)       + "μs  ";
            return std::to_string(ns) + "ns  ";
        };

        std::string label = fmt_ns(lo) + "– " + fmt_ns(hi);
        uint64_t bar_len  = (cnt * max_bar_width) / peak;
        std::string bar(static_cast<size_t>(bar_len), '=');

        std::cout << "  " << std::setw(24) << label
                  << " │" << bar
                  << " " << cnt << "\n";
    }
    std::cout << "\n";
}

void merge_latency_trackers(LatencyTracker& dst, const LatencyTracker& src) {

    for (int i = 0; i < LatencyTracker::NUM_BUCKETS; ++i) {
        uint64_t cnt = src.raw_bucket(i);
        uint64_t val = (i < 63) ? (uint64_t(1) << i) : UINT64_MAX;
        for (uint64_t j = 0; j < cnt; ++j)
            dst.record(val);
    }
}
