#include <gtest/gtest.h>
#include "../src/client/parser.h"
#include "../include/protocol.h"
#include <cstring>

// ─── Helper: build a valid TradeMsg ──────────────────────────────────────────
static TradeMsg make_trade(uint32_t seq, uint16_t sym, double price, uint32_t qty) {
    TradeMsg msg{};
    msg.header.msg_type     = MSG_TRADE;
    msg.header.seq_no       = seq;
    msg.header.timestamp_ns = 123456789;
    msg.header.symbol_id    = sym;
    msg.payload.price       = price;
    msg.payload.quantity    = qty;
    msg.checksum = compute_checksum(&msg, sizeof(TradeMsg) - 4);
    return msg;
}

static QuoteMsg make_quote(uint32_t seq, uint16_t sym) {
    QuoteMsg msg{};
    msg.header.msg_type     = MSG_QUOTE;
    msg.header.seq_no       = seq;
    msg.header.timestamp_ns = 999;
    msg.header.symbol_id    = sym;
    msg.payload.bid_price   = 100.5;
    msg.payload.bid_qty     = 500;
    msg.payload.ask_price   = 100.6;
    msg.payload.ask_qty     = 300;
    msg.checksum = compute_checksum(&msg, sizeof(QuoteMsg) - 4);
    return msg;
}

TEST(Parser, SingleTradeMessage) {
    Parser p;
    TradeMsg msg = make_trade(1, 0, 2500.0, 100);

    int trade_count = 0;
    p.feed(reinterpret_cast<uint8_t*>(&msg), sizeof(msg),
           [&](const uint8_t*, size_t){ trade_count++; },
           nullptr, nullptr);

    EXPECT_EQ(trade_count, 1);
    EXPECT_EQ(p.total_parsed(), 1u);
    EXPECT_EQ(p.seq_gaps(), 0u);
}

TEST(Parser, FragmentedMessage) {
    Parser p;
    TradeMsg msg = make_trade(1, 0, 1000.0, 50);

    // Feed in 3 fragments
    auto* raw = reinterpret_cast<uint8_t*>(&msg);
    int trade_count = 0;
    auto cb = [&](const uint8_t*, size_t){ trade_count++; };

    p.feed(raw,     5,  cb, nullptr, nullptr);
    EXPECT_EQ(trade_count, 0);
    p.feed(raw+5,   10, cb, nullptr, nullptr);
    EXPECT_EQ(trade_count, 0);
    p.feed(raw+15, sizeof(TradeMsg)-15, cb, nullptr, nullptr);
    EXPECT_EQ(trade_count, 1);
}

TEST(Parser, MultipleMessages) {
    Parser p;
    std::vector<uint8_t> buf;
    for (int i = 1; i <= 5; ++i) {
        auto msg = make_trade(i, 0, 100.0*i, i*10);
        auto* raw = reinterpret_cast<uint8_t*>(&msg);
        buf.insert(buf.end(), raw, raw + sizeof(TradeMsg));
    }

    int count = 0;
    p.feed(buf.data(), buf.size(),
           [&](const uint8_t*, size_t){ count++; },
           nullptr, nullptr);
    EXPECT_EQ(count, 5);
}

TEST(Parser, SequenceGapDetected) {
    Parser p;
    auto m1 = make_trade(1, 0, 100.0, 10);
    auto m2 = make_trade(5, 0, 101.0, 10);  // gap of 3

    int count = 0;
    auto cb = [&](const uint8_t*, size_t){ count++; };
    p.feed(reinterpret_cast<uint8_t*>(&m1), sizeof(m1), cb, nullptr, nullptr);
    p.feed(reinterpret_cast<uint8_t*>(&m2), sizeof(m2), cb, nullptr, nullptr);

    EXPECT_EQ(count, 2);
    EXPECT_EQ(p.seq_gaps(), 3u);
}

TEST(Parser, BadChecksumIgnored) {
    Parser p;
    TradeMsg msg = make_trade(1, 0, 200.0, 20);
    msg.checksum ^= 0xDEADBEEF;  // corrupt

    int count = 0;
    p.feed(reinterpret_cast<uint8_t*>(&msg), sizeof(msg),
           [&](const uint8_t*, size_t){ count++; },
           nullptr, nullptr);

    EXPECT_EQ(count, 0);
    EXPECT_EQ(p.bad_checksums(), 1u);
}

TEST(Parser, QuoteMessage) {
    Parser p;
    QuoteMsg msg = make_quote(1, 3);

    int quote_count = 0;
    p.feed(reinterpret_cast<uint8_t*>(&msg), sizeof(msg),
           nullptr,
           [&](const uint8_t* data, size_t){
               quote_count++;
               const auto* q = reinterpret_cast<const QuoteMsg*>(data);
               EXPECT_NEAR(q->payload.bid_price, 100.5, 1e-9);
               EXPECT_NEAR(q->payload.ask_price, 100.6, 1e-9);
           },
           nullptr);
    EXPECT_EQ(quote_count, 1);
}

TEST(Parser, MixedStream) {
    Parser p;
    std::vector<uint8_t> buf;

    auto append = [&](auto& msg) {
        auto* raw = reinterpret_cast<uint8_t*>(&msg);
        buf.insert(buf.end(), raw, raw + sizeof(msg));
    };

    auto t = make_trade(1, 0, 100.0, 10);
    auto q = make_quote(2, 1);
    auto t2 = make_trade(3, 2, 200.0, 20);
    append(t); append(q); append(t2);

    int trades = 0, quotes = 0;
    p.feed(buf.data(), buf.size(),
           [&](const uint8_t*, size_t){ trades++; },
           [&](const uint8_t*, size_t){ quotes++; },
           nullptr);
    EXPECT_EQ(trades, 2);
    EXPECT_EQ(quotes, 1);
}
