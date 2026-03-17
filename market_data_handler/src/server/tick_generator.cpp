#include "tick_generator.h"
#include <cmath>
#include <cstring>
#include <chrono>
#include <stdexcept>

double TickGenerator::normal_sample() noexcept {
   
    if (has_spare_) {
        has_spare_ = false;
        return spare_;
    }
    double u, v, s;
    do {
        u = dist_(rng_) * 2.0 - 1.0;
        v = dist_(rng_) * 2.0 - 1.0;
        s = u*u + v*v;
    } while (s >= 1.0 || s == 0.0);

    double mul = std::sqrt(-2.0 * std::log(s) / s);
    spare_ = v * mul;
    has_spare_ = true;
    return u * mul;
}

TickGenerator::TickGenerator(size_t num_symbols, uint64_t seed)
    : rng_(seed), dist_(0.0, 1.0)
{
    symbols_.reserve(num_symbols);
    std::uniform_real_distribution<double> price_dist(100.0, 5000.0);
    std::uniform_real_distribution<double> vol_dist(0.01, 0.06);

    for (size_t i = 0; i < num_symbols; ++i) {
        SymbolState s{};
        s.price      = price_dist(rng_);
        s.initial_price = s.price;
        s.sigma      = vol_dist(rng_);
       
        double r = dist_(rng_);
        if      (r < 0.15) s.mu =  0.05; 
        else if (r < 0.30) s.mu = -0.05; 
        else               s.mu =  0.00;  
        symbols_.push_back(s);
    }
}

void TickGenerator::step(uint16_t symbol_id) noexcept {
    auto& s = symbols_[symbol_id];
    double dt   = DT;
    double Z    = normal_sample();
    double exponent = (s.mu - 0.5 * s.sigma * s.sigma) * dt
                    + s.sigma * std::sqrt(dt) * Z;
    s.price *= std::exp(exponent);
    if (s.price < 1.0)    s.price = 1.0;
    if (s.price > 1e6)   s.price = 1e6;
}

QuoteMsg TickGenerator::make_quote(uint16_t sym, uint32_t seq) noexcept {
    step(sym);
    auto& s = symbols_[sym];

    double spread_frac = 0.0005 + dist_(rng_) * 0.0015;
    double half_spread = s.price * spread_frac / 2.0;

    QuoteMsg msg{};
    msg.header.msg_type    = MSG_QUOTE;
    msg.header.seq_no      = seq;
    msg.header.timestamp_ns = now_ns();
    msg.header.symbol_id   = sym;
    msg.payload.bid_price  = s.price - half_spread;
    msg.payload.ask_price  = s.price + half_spread;

    std::lognormal_distribution<double> vol_dist(6.9, 0.5); 
    msg.payload.bid_qty = static_cast<uint32_t>(vol_dist(rng_));
    msg.payload.ask_qty = static_cast<uint32_t>(vol_dist(rng_));

    msg.checksum = compute_checksum(&msg, sizeof(QuoteMsg) - sizeof(uint32_t));
    return msg;
}

TradeMsg TickGenerator::make_trade(uint16_t sym, uint32_t seq) noexcept {
    step(sym);
    auto& s = symbols_[sym];

    TradeMsg msg{};
    msg.header.msg_type    = MSG_TRADE;
    msg.header.seq_no      = seq;
    msg.header.timestamp_ns = now_ns();
    msg.header.symbol_id   = sym;
    msg.payload.price      = s.price;

    std::lognormal_distribution<double> vol_dist(5.5, 0.8);
    msg.payload.quantity = static_cast<uint32_t>(vol_dist(rng_));

    msg.checksum = compute_checksum(&msg, sizeof(TradeMsg) - sizeof(uint32_t));
    return msg;
}

HeartbeatMsg TickGenerator::make_heartbeat(uint32_t seq) noexcept {
    HeartbeatMsg msg{};
    msg.header.msg_type    = MSG_HEARTBEAT;
    msg.header.seq_no      = seq;
    msg.header.timestamp_ns = now_ns();
    msg.header.symbol_id   = 0;
    msg.checksum = compute_checksum(&msg, sizeof(HeartbeatMsg) - sizeof(uint32_t));
    return msg;
}

uint64_t TickGenerator::now_ns() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

double TickGenerator::current_price(uint16_t sym) const noexcept {
    return symbols_[sym].price;
}
