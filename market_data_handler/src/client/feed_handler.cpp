#include "socket.h"
#include "parser.h"
#include "visualizer.h"
#include "../../include/cache.h"
#include "../../include/latency_tracker.h"
#include <iostream>
#include <csignal>
#include <cstring>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

static std::atomic<bool> g_running{true};

static void sig_handler(int) { g_running.store(false); }

static std::vector<std::string> make_symbol_names(size_t n) {
    static const char* nse_names[] = {
        "RELIANCE","TCS","INFY","HDFC","ICICIBANK","HDFCBANK","BAJFINANCE",
        "WIPRO","SBIN","BHARTIARTL","AXISBANK","ADANIENT","ADANIPORTS",
        "ASIANPAINT","BAJAJFINSV","BPCL","BRITANNIA","CIPLA","COALINDIA",
        "DIVISLAB","DRREDDY","EICHERMOT","GRASIM","HCLTECH","HDFCLIFE",
        "HEROMOTOCO","HINDALCO","HINDUNILVR","ITC","INDUSINDBK",
        "JSWSTEEL","KOTAKBANK","LT","M&M","MARUTI","NESTLEIND","NTPC",
        "ONGC","POWERGRID","SBILIFE","SHREECEM","SUNPHARMA","TATACONSUM",
        "TATAMOTORS","TATASTEEL","TECHM","TITAN","ULTRACEMCO","UPL","VEDL"
    };
    static const size_t known = sizeof(nse_names)/sizeof(nse_names[0]);

    std::vector<std::string> names;
    names.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (i < known) names.push_back(nse_names[i]);
        else           names.push_back("SYM" + std::to_string(i));
    }
    return names;
}

int main(int argc, char* argv[]) {
    std::string host   = "127.0.0.1";
    uint16_t    port   = 9876;
    size_t      num_sym = 100;
    bool        no_viz = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--host"    && i+1 < argc) host    = argv[++i];
        if (a == "--port"    && i+1 < argc) port    = std::stoi(argv[++i]);
        if (a == "--symbols" && i+1 < argc) num_sym = std::stoul(argv[++i]);
        if (a == "--no-viz")                no_viz  = true;
    }

    std::signal(SIGINT,  sig_handler);
    std::signal(SIGTERM, sig_handler);
    std::signal(SIGPIPE, SIG_IGN);

    SymbolCache    cache;
    LatencyTracker lat;
    auto           names = make_symbol_names(num_sym);

    Visualizer viz(cache, lat, names);
    if (!no_viz) viz.start();

    auto on_trade = [&](const uint8_t* data, size_t) {
        const auto* msg = reinterpret_cast<const TradeMsg*>(data);
        uint64_t recv_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        lat.record(recv_ns - msg->header.timestamp_ns);
        cache.update_trade(msg->header.symbol_id,
                           msg->payload.price,
                           msg->payload.quantity,
                           msg->header.timestamp_ns);
        viz.increment_messages();
    };

    auto on_quote = [&](const uint8_t* data, size_t) {
        const auto* msg = reinterpret_cast<const QuoteMsg*>(data);
        uint64_t recv_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        lat.record(recv_ns - msg->header.timestamp_ns);
        cache.update_quote(msg->header.symbol_id,
                           msg->payload.bid_price, msg->payload.bid_qty,
                           msg->payload.ask_price, msg->payload.ask_qty,
                           msg->header.timestamp_ns);
        viz.increment_messages();
    };

    auto on_hb = [&](const uint8_t*, size_t) {
        viz.increment_messages();
    };

    RetrySocket retry(host, port, /*max_retries=*/10, /*backoff_ms=*/200);
    Parser      parser;

    static constexpr size_t RECV_BUF = 65536;
    std::vector<uint8_t> recv_buf(RECV_BUF);

    while (g_running.load()) {
        // Connect (with retry)
        if (!retry.connect_with_retry()) {
            std::cerr << "[FeedHandler] Could not connect – giving up\n";
            break;
        }

        auto& sock = retry.socket();

        // Subscribe to all symbols
        std::vector<uint16_t> sym_ids;
        sym_ids.reserve(num_sym);
        for (uint16_t i = 0; i < static_cast<uint16_t>(num_sym); ++i)
            sym_ids.push_back(i);
        sock.send_subscription(sym_ids);

        parser.reset();

        // Receive loop
        while (g_running.load() && sock.is_connected()) {
            int ev = sock.wait_for_data(100 /*ms*/);
            if (ev < 0) break;
            if (ev == 0) continue; 

            while (true) {
                size_t n = sock.receive(recv_buf.data(), RECV_BUF);
                if (n < 0) goto reconnect;
                if (n == 0) break; 

                parser.feed(recv_buf.data(), static_cast<size_t>(n),
                            on_trade, on_quote, on_hb);
            }
        }
        reconnect:
        std::cout << "[FeedHandler] Disconnected – reconnecting...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    viz.stop();

    auto st = lat.get_stats();
    std::cout << "\n=== Final Statistics ===\n"
              << "  Total messages  : " << st.sample_count << "\n"
              << "  Sequence gaps   : " << parser.seq_gaps() << "\n"
              << "  Bad checksums   : " << parser.bad_checksums() << "\n";
    if (st.sample_count > 0) {
        std::cout << "  Latency p50     : " << st.p50_ns/1000  << " μs\n"
                  << "  Latency p99     : " << st.p99_ns/1000  << " μs\n"
                  << "  Latency p999    : " << st.p999_ns/1000 << " μs\n";
    }
    lat.export_csv("latency_histogram.csv");
    std::cout << "  Histogram saved : latency_histogram.csv\n";
    return 0;
}
