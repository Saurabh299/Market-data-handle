#pragma once
#include "../../include/protocol.h"
#include <cstdint>
#include <functional>

static constexpr size_t PARSE_BUF_SIZE = 128 * 1024;

using ParseCallback = std::function<void(const uint8_t*, size_t)>;

class Parser {
public:
    Parser();

    void feed(const uint8_t* data, size_t len,
              ParseCallback on_trade,
              ParseCallback on_quote,
              ParseCallback on_heartbeat);

    void reset();

    uint64_t total_parsed()  const { return total_parsed_; }
    uint64_t seq_gaps()      const { return seq_gaps_; }
    uint64_t bad_checksums() const { return bad_checksums_; }
    uint32_t last_seq()      const { return last_seq_; }

private:
    alignas(64) uint8_t buf_[PARSE_BUF_SIZE];
    size_t   buf_used_;
    uint32_t last_seq_;
    uint64_t total_parsed_;
    uint64_t seq_gaps_;
    uint64_t bad_checksums_;
};
