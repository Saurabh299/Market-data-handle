#pragma once
#include <cstdint>
#include <cstring>

static constexpr uint16_t MSG_TRADE     = 0x0001;
static constexpr uint16_t MSG_QUOTE     = 0x0002;
static constexpr uint16_t MSG_HEARTBEAT = 0x0003;

static constexpr uint8_t CMD_SUBSCRIBE  = 0xFF;


#pragma pack(push, 1)

struct MsgHeader {
    uint16_t msg_type;
    uint32_t seq_no;
    uint64_t timestamp_ns;
    uint16_t symbol_id;
};
static_assert(sizeof(MsgHeader) == 16, "MsgHeader size mismatch");

struct TradePay {
    double   price;
    uint32_t quantity;
};
static_assert(sizeof(TradePay) == 12, "TradePay size mismatch");

struct QuotePay {
    double   bid_price;
    uint32_t bid_qty;
    double   ask_price;
    uint32_t ask_qty;
};
static_assert(sizeof(QuotePay) == 24, "QuotePay size mismatch");

struct TradeMsg {
    MsgHeader header;
    TradePay  payload;
    uint32_t  checksum;
};
static_assert(sizeof(TradeMsg) == 32, "TradeMsg size mismatch");

struct QuoteMsg {
    MsgHeader header;
    QuotePay  payload;
    uint32_t  checksum;
};
static_assert(sizeof(QuoteMsg) == 44, "QuoteMsg size mismatch");

struct HeartbeatMsg {
    MsgHeader header;
    uint32_t  checksum;
};
static_assert(sizeof(HeartbeatMsg) == 20, "HeartbeatMsg size mismatch");

struct SubscribeMsg {
    uint8_t  command;     
    uint16_t count;
};

#pragma pack(pop)

static constexpr size_t MAX_MSG_SIZE = sizeof(QuoteMsg);

inline uint32_t compute_checksum(const void* data, size_t len) {
    uint32_t cs = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i)
        cs ^= (static_cast<uint32_t>(p[i]) << ((i % 4) * 8));
    return cs;
}

inline size_t msg_total_size(uint16_t msg_type) {
    switch (msg_type) {
        case MSG_TRADE:     return sizeof(TradeMsg);
        case MSG_QUOTE:     return sizeof(QuoteMsg);
        case MSG_HEARTBEAT: return sizeof(HeartbeatMsg);
        default:            return 0;
    }
}
