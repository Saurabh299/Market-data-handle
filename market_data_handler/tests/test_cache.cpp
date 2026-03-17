#include <gtest/gtest.h>
#include "../include/cache.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

TEST(SymbolCache, BasicUpdateAndRead) {
    SymbolCache cache;
    cache.update_quote(0, 100.5, 500, 100.6, 300, 123456789ULL);

    auto snap = cache.get_snapshot(0);
    EXPECT_NEAR(snap.best_bid,   100.5, 1e-9);
    EXPECT_NEAR(snap.best_ask,   100.6, 1e-9);
    EXPECT_EQ  (snap.bid_quantity, 500u);
    EXPECT_EQ  (snap.ask_quantity, 300u);
    EXPECT_EQ  (snap.update_count,   1u);
}

TEST(SymbolCache, TradeUpdate) {
    SymbolCache cache;
    cache.update_trade(5, 2450.75, 1000, 999999ULL);

    auto snap = cache.get_snapshot(5);
    EXPECT_NEAR(snap.last_traded_price,    2450.75, 1e-9);
    EXPECT_EQ  (snap.last_traded_quantity, 1000u);
    EXPECT_EQ  (snap.update_count,            1u);
}

TEST(SymbolCache, IndependentSymbols) {
    SymbolCache cache;
    cache.update_quote(0,  100.0, 100, 101.0, 200, 1ULL);
    cache.update_quote(10, 500.0, 50,  501.0, 75,  2ULL);

    auto s0  = cache.get_snapshot(0);
    auto s10 = cache.get_snapshot(10);

    EXPECT_NEAR(s0.best_bid,  100.0, 1e-9);
    EXPECT_NEAR(s10.best_bid, 500.0, 1e-9);
}

TEST(SymbolCache, MultipleUpdatesCountCorrectly) {
    SymbolCache cache;
    for (int i = 0; i < 100; ++i)
        cache.update_bid(3, 200.0 + i, 1000, static_cast<uint64_t>(i));

    auto snap = cache.get_snapshot(3);
    EXPECT_EQ(snap.update_count, 100u);
    EXPECT_NEAR(snap.best_bid, 299.0, 1e-9);
}

// Single writer, multiple reader threads – should never see torn state
TEST(SymbolCache, ConcurrentReadWrite) {
    SymbolCache cache;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> bad_reads{0};

    // Writer thread
    std::thread writer([&]{
        uint64_t ts = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            cache.update_quote(7, 1000.0, 100, 1001.0, 200, ts++);
        }
    });

    // 4 reader threads
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&]{
            while (!stop.load(std::memory_order_relaxed)) {
                auto snap = cache.get_snapshot(7);
                // bid must be <= ask if both non-zero
                if (snap.best_bid > 0 && snap.best_ask > 0 &&
                    snap.best_bid > snap.best_ask + 10.0) {
                    bad_reads.fetch_add(1);
                }
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop.store(true);
    writer.join();
    for (auto& t : readers) t.join();

    EXPECT_EQ(bad_reads.load(), 0u) << "Torn reads detected!";
}

TEST(SymbolCache, ReadLatencyUnder50ns) {
    SymbolCache cache;
    cache.update_quote(0, 100.0, 100, 101.0, 200, 1ULL);

    static constexpr int ITERS = 100000;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < ITERS; ++i) {
        volatile auto snap = cache.get_snapshot(0);
        (void)snap;
    }
    auto end = std::chrono::steady_clock::now();

    double avg_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        end - start).count() / double(ITERS);
    std::cout << "  SymbolCache avg read latency: " << avg_ns << " ns\n";
    EXPECT_LT(avg_ns, 200.0) << "Read latency exceeded 200ns (target <50ns in co-lo)";
}
