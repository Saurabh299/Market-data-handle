#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

struct alignas(64) MarketState {
    std::atomic<uint32_t> version{0};   
    double   best_bid{0.0};
    double   best_ask{0.0};
    uint32_t bid_quantity{0};
    uint32_t ask_quantity{0};
    double   last_traded_price{0.0};
    uint32_t last_traded_quantity{0};
    uint64_t last_update_time{0};
    uint64_t update_count{0};

    MarketState() = default;

    MarketState(const MarketState& o)
        : version(o.version.load(std::memory_order_relaxed))
        , best_bid(o.best_bid), best_ask(o.best_ask)
        , bid_quantity(o.bid_quantity), ask_quantity(o.ask_quantity)
        , last_traded_price(o.last_traded_price)
        , last_traded_quantity(o.last_traded_quantity)
        , last_update_time(o.last_update_time)
        , update_count(o.update_count) {}

    MarketState& operator=(const MarketState& o) {
        version.store(o.version.load(std::memory_order_relaxed),
                      std::memory_order_relaxed);
        best_bid             = o.best_bid;
        best_ask             = o.best_ask;
        bid_quantity         = o.bid_quantity;
        ask_quantity         = o.ask_quantity;
        last_traded_price    = o.last_traded_price;
        last_traded_quantity = o.last_traded_quantity;
        last_update_time     = o.last_update_time;
        update_count         = o.update_count;
        return *this;
    }
};

class SymbolCache {
public:
    static constexpr uint16_t MAX_SYMBOLS = 500;

    SymbolCache() = default; 

    void update_bid(uint16_t sym, double price, uint32_t qty, uint64_t ts) noexcept {
        auto& s = states_[sym];
        begin_write(s);
        s.best_bid     = price;
        s.bid_quantity = qty;
        s.last_update_time = ts;
        s.update_count++;
        end_write(s);
    }

    void update_ask(uint16_t sym, double price, uint32_t qty, uint64_t ts) noexcept {
        auto& s = states_[sym];
        begin_write(s);
        s.best_ask     = price;
        s.ask_quantity = qty;
        s.last_update_time = ts;
        s.update_count++;
        end_write(s);
    }

    void update_quote(uint16_t sym, double bid, uint32_t bqty,
                      double ask, uint32_t aqty, uint64_t ts) noexcept {
        auto& s = states_[sym];
        begin_write(s);
        s.best_bid     = bid;
        s.bid_quantity = bqty;
        s.best_ask     = ask;
        s.ask_quantity = aqty;
        s.last_update_time = ts;
        s.update_count++;
        end_write(s);
    }

    void update_trade(uint16_t sym, double price, uint32_t qty, uint64_t ts) noexcept {
        auto& s = states_[sym];
        begin_write(s);
        s.last_traded_price    = price;
        s.last_traded_quantity = qty;
        s.last_update_time     = ts;
        s.update_count++;
        end_write(s);
    }

    MarketState get_snapshot(uint16_t sym) const noexcept {
        const auto& s = states_[sym];
        MarketState snap;
        uint32_t v1, v2 = 0;
        do {
            v1 = s.version.load(std::memory_order_acquire);
            if (v1 & 1) continue;  
            snap = s;              
            v2 = s.version.load(std::memory_order_acquire);
        } while (v1 != v2);
        return snap;
    }

    const MarketState* raw(uint16_t sym) const noexcept { return &states_[sym]; }

private:
    MarketState states_[MAX_SYMBOLS];

    static void begin_write(MarketState& s) noexcept {
        s.version.fetch_add(1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
    static void end_write(MarketState& s) noexcept {
        std::atomic_thread_fence(std::memory_order_release);
        s.version.fetch_add(1, std::memory_order_release);
    }
};
