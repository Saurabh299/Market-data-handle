#pragma once
#include "../../include/cache.h"
#include "../../include/latency_tracker.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

class Visualizer {
public:
    Visualizer(SymbolCache& cache,
               const LatencyTracker& lat,
               std::vector<std::string> symbol_names);
    ~Visualizer();

    void start();
    void stop();

    void increment_messages(uint64_t n = 1);

private:
    void loop();
    void render();

    SymbolCache&          cache_;
    const LatencyTracker& lat_;
    std::vector<std::string> symbol_names_;
    std::thread           thread_;
    std::atomic<bool>     running_;
    std::atomic<uint64_t> total_messages_;
    std::chrono::steady_clock::time_point start_time_;
};
