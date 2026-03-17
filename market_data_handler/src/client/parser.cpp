#include "parser.h"
#include <cstring>
#include <iostream>

Parser::Parser()
    : buf_used_(0)
    , last_seq_(0)
    , total_parsed_(0)
    , seq_gaps_(0)
    , bad_checksums_(0)
{}

void Parser::feed(const uint8_t* data, size_t len,
                  ParseCallback on_trade,
                  ParseCallback on_quote,
                  ParseCallback on_heartbeat)
{
    if (buf_used_ + len > sizeof(buf_)) {

        size_t space = sizeof(buf_) - buf_used_;
        if (space > 0) {
            std::memcpy(buf_ + buf_used_, data, space);
            buf_used_ += space;
        }
        std::cerr << "[Parser] Buffer overflow – discarding " << (len - space) << " bytes\n";
     
    } else {
        std::memcpy(buf_ + buf_used_, data, len);
        buf_used_ += len;
    }

    size_t offset = 0;
    while (buf_used_ - offset >= sizeof(MsgHeader)) {
        const auto* hdr = reinterpret_cast<const MsgHeader*>(buf_ + offset);

        size_t expected = msg_total_size(hdr->msg_type);
        if (expected == 0) {
            ++offset;
            continue;
        }
        if (buf_used_ - offset < expected) break;

        uint32_t computed = compute_checksum(buf_ + offset, expected - 4);
        uint32_t wire_cs;
        std::memcpy(&wire_cs, buf_ + offset + expected - 4, 4);
        if (computed != wire_cs) {
            bad_checksums_++;
            offset += expected;  
            continue;
        }

        uint32_t seq = hdr->seq_no;
        if (last_seq_ > 0 && seq != last_seq_ + 1 && seq > last_seq_ + 1) {
            seq_gaps_ += seq - last_seq_ - 1;
            std::cerr << "[Parser] Sequence gap: expected " << (last_seq_+1)
                      << " got " << seq << "\n";
        }
        last_seq_ = seq;
        total_parsed_++;

        switch (hdr->msg_type) {
            case MSG_TRADE:
                if (on_trade) on_trade(buf_ + offset, expected);
                break;
            case MSG_QUOTE:
                if (on_quote) on_quote(buf_ + offset, expected);
                break;
            case MSG_HEARTBEAT:
                if (on_heartbeat) on_heartbeat(buf_ + offset, expected);
                break;
        }

        offset += expected;
    }

    if (offset > 0) {
        buf_used_ -= offset;
        if (buf_used_ > 0)
            std::memmove(buf_, buf_ + offset, buf_used_);
    }
}

void Parser::reset() {
    buf_used_      = 0;
    last_seq_      = 0;
    total_parsed_  = 0;
    seq_gaps_      = 0;
    bad_checksums_ = 0;
}
