#include "visualizer.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <sys/ioctl.h>
#include <unistd.h>

static const char* GREEN  = "\033[32m";
static const char* RED    = "\033[31m";
static const char* YELLOW = "\033[33m";
static const char* CYAN   = "\033[36m";
static const char* BOLD   = "\033[1m";
static const char* RESET  = "\033[0m";
static const char* CLEAR  = "\033[2J\033[H";

Visualizer::Visualizer(SymbolCache& cache,
                       const LatencyTracker& lat,
                       std::vector<std::string> names)
    : cache_(cache), lat_(lat), symbol_names_(std::move(names))
    , running_(false)
    , total_messages_(0)
    , start_time_(std::chrono::steady_clock::now())
{}

Visualizer::~Visualizer() { stop(); }

void Visualizer::start() {
    running_.store(true);
    thread_ = std::thread(&Visualizer::loop, this);
}

void Visualizer::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

void Visualizer::increment_messages(uint64_t n) {
    total_messages_.fetch_add(n, std::memory_order_relaxed);
}

void Visualizer::loop() {
    while (running_.load(std::memory_order_relaxed)) {
        render();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

static std::string fmt_price(double p) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << p;
    return ss.str();
}

static std::string fmt_large(uint64_t n) {
  
    std::string s = std::to_string(n);
    int ins = static_cast<int>(s.size()) - 3;
    while (ins > 0) { s.insert(ins, ","); ins -= 3; }
    return s;
}

void Visualizer::render() {
    std::vector<std::pair<uint64_t, uint16_t>> ranked;
    ranked.reserve(symbol_names_.size());

    for (size_t i = 0; i < symbol_names_.size(); ++i) {
        auto snap = cache_.get_snapshot(static_cast<uint16_t>(i));
        ranked.emplace_back(snap.update_count, static_cast<uint16_t>(i));
    }

    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b){ return a.first > b.first; });
    if (ranked.size() > 20) ranked.resize(20);

    // Uptime
    auto now  = std::chrono::steady_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
    int  hh   = secs / 3600, mm = (secs % 3600) / 60, ss = secs % 60;

    uint64_t total = total_messages_.load(std::memory_order_relaxed);
    uint64_t rate  = (secs > 0) ? total / secs : 0;

    auto stats = lat_.get_stats();

    // Build output string (write all at once to reduce flicker)
    std::ostringstream out;
    out << CLEAR;
    out << BOLD << CYAN
        << "═══════════════════════════════════════════════════════════════════\n"
        << "         NSE Market Data Feed Handler  –  Live Dashboard\n"
        << "═══════════════════════════════════════════════════════════════════\n"
        << RESET;

    out << " Uptime: " << std::setw(2) << std::setfill('0') << hh << ":"
        << std::setw(2) << mm << ":" << std::setw(2) << ss
        << "  │  Messages: " << fmt_large(total)
        << "  │  Rate: " << fmt_large(rate) << " msg/s\n\n";

    out << BOLD
        << std::left  << std::setw(14) << " Symbol"
        << std::right << std::setw(12) << "Bid"
        << std::right << std::setw(12) << "Ask"
        << std::right << std::setw(12) << "LTP"
        << std::right << std::setw(12) << "Chg%"
        << std::right << std::setw(10) << "Updates"
        << "\n" << RESET;
    out << " ──────────────────────────────────────────────────────────────────\n";

    for (auto& [cnt, id] : ranked) {
        auto s = cache_.get_snapshot(id);
        if (s.update_count == 0) continue;

        std::string name = (id < symbol_names_.size())
                           ? symbol_names_[id] : ("SYM" + std::to_string(id));
        if (name.size() > 12) name = name.substr(0, 12);

        double chg_pct = 0.0;
        if (s.best_bid > 0 && s.best_ask > 0)
            chg_pct = (s.best_ask - s.best_bid) / s.best_bid * 100.0;

        const char* col = (s.best_ask >= s.best_bid) ? GREEN : RED;

        out << " " << col << BOLD << std::left << std::setw(13) << name << RESET
            << col
            << std::right << std::setw(12) << fmt_price(s.best_bid)
            << std::right << std::setw(12) << fmt_price(s.best_ask)
            << std::right << std::setw(12) << fmt_price(s.last_traded_price > 0 ? s.last_traded_price : s.best_bid)
            << RESET
            << std::right << std::setw(11)
            << (std::string(chg_pct >= 0 ? GREEN : RED)
                + (chg_pct >= 0 ? "+" : "")
                + std::to_string(static_cast<int>(chg_pct * 100) / 100.0).substr(0,5) + "%"
                + RESET)
            << std::right << std::setw(10) << fmt_large(s.update_count)
            << "\n";
    }

    out << "\n " << BOLD << "Statistics:" << RESET << "\n";
    out << "  Parser Throughput  : " << fmt_large(rate) << " msg/s\n";
    if (stats.sample_count > 0) {
        out << "  End-to-End Latency : "
            << "p50=" << stats.p50_ns/1000 << "μs  "
            << "p99=" << stats.p99_ns/1000 << "μs  "
            << "p999=" << stats.p999_ns/1000 << "μs\n";
        out << "  Min/Max            : " << stats.min_ns/1000 << "μs / "
            << stats.max_ns/1000 << "μs\n";
    }
    out << "\n " << YELLOW << "Press Ctrl+C to quit" << RESET << "\n";

    std::cout << out.str() << std::flush;
}
