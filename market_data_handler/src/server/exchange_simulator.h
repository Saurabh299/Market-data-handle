#pragma once
#include "tick_generator.h"
#include <atomic>
#include <set>
#include <unordered_set>
#include <cstdint>

class ExchangeSimulator {
public:
    ExchangeSimulator(uint16_t port, size_t num_symbols = 100);
    ~ExchangeSimulator();

    void start();
    void run();
    void stop() { running_.store(false); }

    void set_tick_rate(uint32_t ticks_per_second);
    void enable_fault_injection(bool enable);

private:
    void handle_new_connection();
    void generate_and_broadcast();
    void broadcast_message(const void* data, size_t len);
    void handle_client_disconnect(int client_fd);

    uint16_t      port_;
    size_t        symbol_count_;
    TickGenerator gen_;

    int server_fd_{-1};
    int epoll_fd_{-1};

    std::unordered_set<int> clients_;
    std::unordered_set<int> slow_clients_;

    std::atomic<bool>     running_;
    std::atomic<uint32_t> tick_rate_;
    std::atomic<bool>     fault_injection_;
    uint32_t              seq_no_;
};
