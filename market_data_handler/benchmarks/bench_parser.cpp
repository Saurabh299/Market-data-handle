// benchmarks/bench_parser.cpp
// Measures pure parser throughput in messages/second
#include "../src/client/parser.h"
#include "../include/protocol.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>

static TradeMsg make_trade(uint32_t seq) {
    TradeMsg msg{};
    msg.header.msg_type     = MSG_TRADE;
    msg.header.seq_no       = seq;
    msg.header.timestamp_ns = static_cast<uint64_t>(seq) * 1000;
    msg.header.symbol_id    = seq % 100;
    msg.payload.price       = 1000.0 + seq % 5000;
    msg.payload.quantity    = 100 + seq % 900;
    msg.checksum = compute_checksum(&msg, sizeof(TradeMsg) - 4);
    return msg;
}

int main() {
    static constexpr int N_MSGS   = 1'000'000;
    static constexpr int FRAG_SZ  = 7;   // simulate fragmentation

    // Pre-build stream
    std::vector<uint8_t> stream;
    stream.reserve(N_MSGS * sizeof(TradeMsg));
    for (int i = 1; i <= N_MSGS; ++i) {
        TradeMsg m = make_trade(i);
        auto* raw = reinterpret_cast<uint8_t*>(&m);
        stream.insert(stream.end(), raw, raw + sizeof(TradeMsg));
    }

    // ── Benchmark 1: 64KB chunk feed (realistic recv() size) ─────────────────
    {
        Parser p;
        uint64_t count = 0;
        auto cb = [&](const uint8_t*, size_t){ count++; };

        static constexpr size_t CHUNK = 65536;
        auto t0 = std::chrono::steady_clock::now();
        size_t off = 0;
        while (off < stream.size()) {
            size_t chunk = std::min(CHUNK, stream.size() - off);
            p.feed(stream.data() + off, chunk, cb, nullptr, nullptr);
            off += chunk;
        }
        auto t1 = std::chrono::steady_clock::now();

        double ms  = std::chrono::duration<double, std::milli>(t1-t0).count();
        double mps = count / (ms / 1000.0);
        std::cout << "[Bench] 64KB-chunk feed (realistic recv size)\n"
                  << "  Messages parsed : " << count << "\n"
                  << "  Time            : " << ms << " ms\n"
                  << "  Throughput      : " << static_cast<uint64_t>(mps) << " msg/s\n\n";
    }

    // ── Benchmark 2: fragmented feed (7-byte chunks) ──────────────────────────
    {
        Parser p;
        uint64_t count = 0;
        auto cb = [&](const uint8_t*, size_t){ count++; };

        auto t0 = std::chrono::steady_clock::now();
        size_t off = 0;
        while (off < stream.size()) {
            size_t chunk = std::min<size_t>(FRAG_SZ, stream.size() - off);
            p.feed(stream.data() + off, chunk, cb, nullptr, nullptr);
            off += chunk;
        }
        auto t1 = std::chrono::steady_clock::now();

        double ms  = std::chrono::duration<double, std::milli>(t1-t0).count();
        double mps = count / (ms / 1000.0);
        std::cout << "[Bench] Fragmented feed (" << FRAG_SZ << "-byte chunks)\n"
                  << "  Messages parsed : " << count << "\n"
                  << "  Time            : " << ms << " ms\n"
                  << "  Throughput      : " << static_cast<uint64_t>(mps) << " msg/s\n\n";
    }

    return 0;
}
