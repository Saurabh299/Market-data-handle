#pragma once
#include "../../include/protocol.h"
#include <vector>
#include <random>
#include <cstdint>

class TickGenerator {
public:
    static constexpr double DT = 0.001; // millisecond time step

    explicit TickGenerator(size_t num_symbols, uint64_t seed = 42);

    // Generate next message for symbol
    QuoteMsg     make_quote(uint16_t sym, uint32_t seq) noexcept;
    TradeMsg     make_trade(uint16_t sym, uint32_t seq) noexcept;
    HeartbeatMsg make_heartbeat(uint32_t seq) noexcept;

    double current_price(uint16_t sym) const noexcept;
    size_t num_symbols() const noexcept { return symbols_.size(); }

private:
    struct SymbolState {
        double price;
        double initial_price;
        double mu;    // drift
        double sigma; // volatility
    };

    std::mt19937_64                        rng_;
    std::uniform_real_distribution<double> dist_;
    std::vector<SymbolState>               symbols_;

    // Box-Muller cached second value
    bool   has_spare_{false};
    double spare_{0.0};

    double   normal_sample() noexcept;
    void     step(uint16_t symbol_id) noexcept;
    uint64_t now_ns() noexcept;
};
