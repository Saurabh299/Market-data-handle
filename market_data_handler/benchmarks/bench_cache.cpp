
#include "../include/cache.h"
#include "../include/latency_tracker.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

int main() {
    SymbolCache    cache;
    LatencyTracker write_lat;
    LatencyTracker read_lat;

    static constexpr int    SYMBOLS    = 100;
    static constexpr int    WRITE_ITERS = 1'000'000;
    static constexpr int    READ_THREADS = 4;
    std::atomic<bool>       stop{false};
    std::atomic<uint64_t>   reads_done{0};

    std::vector<std::thread> readers;
    for (int t = 0; t < READ_THREADS; ++t) {
        readers.emplace_back([&, t]{
            uint16_t sym = t % SYMBOLS;
            while (!stop.load(std::memory_order_relaxed)) {
                auto t0 = std::chrono::steady_clock::now();
                volatile auto snap = cache.get_snapshot(sym);
                (void)snap;
                auto t1 = std::chrono::steady_clock::now();
                uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count();
                read_lat.record(ns);
                reads_done.fetch_add(1, std::memory_order_relaxed);
                sym = (sym + 1) % SYMBOLS;
            }
        });
    }
─
    auto wall_start = std::chrono::steady_clock::now();
    for (int i = 0; i < WRITE_ITERS; ++i) {
        uint16_t sym = i % SYMBOLS;
        uint64_t ts  = static_cast<uint64_t>(i);

        auto t0 = std::chrono::steady_clock::now();
        if (i % 3 == 0)
            cache.update_quote(sym, 1000.0+i, 100, 1001.0+i, 200, ts);
        else
            cache.update_trade(sym, 1000.5+i, 50, ts);
        auto t1 = std::chrono::steady_clock::now();

        write_lat.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count());
    }
    auto wall_end = std::chrono::steady_clock::now();

    stop.store(true);
    for (auto& r : readers) r.join();

    double wall_s = std::chrono::duration<double>(wall_end - wall_start).count();
    auto ws = write_lat.get_stats();
    auto rs = read_lat.get_stats();

    std::cout << "=== SymbolCache Benchmark ===\n\n";
    std::cout << "[Writer] " << WRITE_ITERS << " updates in "
              << static_cast<int>(wall_s * 1000) << " ms  →  "
              << static_cast<uint64_t>(WRITE_ITERS / wall_s) << " updates/s\n";
    std::cout << "  Write latency  min=" << ws.min_ns << "ns"
              << "  p50=" << ws.p50_ns << "ns"
              << "  p99=" << ws.p99_ns << "ns"
              << "  max=" << ws.max_ns << "ns\n\n";

    std::cout << "[Readers] " << reads_done.load() << " reads across "
              << READ_THREADS << " threads\n";
    std::cout << "  Read latency   min=" << rs.min_ns << "ns"
              << "  p50=" << rs.p50_ns << "ns"
              << "  p99=" << rs.p99_ns << "ns"
              << "  max=" << rs.max_ns << "ns\n";

    write_lat.export_csv("bench_write_lat.csv");
    read_lat.export_csv("bench_read_lat.csv");
    std::cout << "\nHistograms saved to bench_write_lat.csv / bench_read_lat.csv\n";

    return 0;
}
