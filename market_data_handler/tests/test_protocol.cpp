#include <gtest/gtest.h>
#include "../include/protocol.h"
#include <cstring>

TEST(Protocol, MessageSizes) {
    EXPECT_EQ(sizeof(MsgHeader),    16u);
    EXPECT_EQ(sizeof(TradePay),     12u);
    EXPECT_EQ(sizeof(QuotePay),     24u);
    EXPECT_EQ(sizeof(TradeMsg),     32u);
    EXPECT_EQ(sizeof(QuoteMsg),     44u);
    EXPECT_EQ(sizeof(HeartbeatMsg), 20u);
}

TEST(Protocol, TotalSizeHelper) {
    EXPECT_EQ(msg_total_size(MSG_TRADE),     sizeof(TradeMsg));
    EXPECT_EQ(msg_total_size(MSG_QUOTE),     sizeof(QuoteMsg));
    EXPECT_EQ(msg_total_size(MSG_HEARTBEAT), sizeof(HeartbeatMsg));
    EXPECT_EQ(msg_total_size(0xDEAD),        0u);
}

TEST(Protocol, ChecksumRoundtrip) {
    TradeMsg msg{};
    msg.header.msg_type     = MSG_TRADE;
    msg.header.seq_no       = 42;
    msg.header.timestamp_ns = 1234567890ULL;
    msg.header.symbol_id    = 7;
    msg.payload.price       = 3500.25;
    msg.payload.quantity    = 250;
    msg.checksum = compute_checksum(&msg, sizeof(TradeMsg) - 4);

    // Verify: checksum of [header+payload] should equal stored checksum
    uint32_t check = compute_checksum(&msg, sizeof(TradeMsg) - 4);
    EXPECT_EQ(check, msg.checksum);
}

TEST(Protocol, ChecksumDetectsBitFlip) {
    QuoteMsg msg{};
    msg.header.msg_type     = MSG_QUOTE;
    msg.header.seq_no       = 1;
    msg.header.timestamp_ns = 1000ULL;
    msg.header.symbol_id    = 0;
    msg.payload.bid_price   = 100.0;
    msg.payload.bid_qty     = 100;
    msg.payload.ask_price   = 100.1;
    msg.payload.ask_qty     = 100;
    msg.checksum = compute_checksum(&msg, sizeof(QuoteMsg) - 4);

    uint32_t original = msg.checksum;
    msg.payload.bid_price = 200.0;  // tamper
    uint32_t recomputed = compute_checksum(&msg, sizeof(QuoteMsg) - 4);

    EXPECT_NE(original, recomputed);
}

TEST(Protocol, HeartbeatStructure) {
    HeartbeatMsg hb{};
    hb.header.msg_type     = MSG_HEARTBEAT;
    hb.header.seq_no       = 999;
    hb.header.timestamp_ns = 0;
    hb.header.symbol_id    = 0;
    hb.checksum = compute_checksum(&hb, sizeof(HeartbeatMsg) - 4);

    EXPECT_EQ(hb.header.msg_type, MSG_HEARTBEAT);
    EXPECT_EQ(hb.header.seq_no,   999u);
}

TEST(Protocol, MaxMsgSize) {
    EXPECT_EQ(MAX_MSG_SIZE, sizeof(QuoteMsg));
}

TEST(Protocol, SubscribeCommandByte) {
    EXPECT_EQ(CMD_SUBSCRIBE, 0xFFu);
}
