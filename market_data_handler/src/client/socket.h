#pragma once
#include "../../include/protocol.h"
#include <string>
#include <vector>
#include <atomic>
#include <cstdint>

class MarketDataSocket {
public:
    MarketDataSocket();
    ~MarketDataSocket();

    bool connect(const std::string& host, uint16_t port,
                 uint32_t timeout_ms = 5000);

    size_t receive(void* buffer, size_t max_len);

    bool send_subscription(const std::vector<uint16_t>& symbol_ids);

    int wait_for_data(int timeout_ms = 100);

    bool is_connected() const;
    void disconnect();

    bool set_tcp_nodelay(bool enable);
    bool set_recv_buffer_size(size_t bytes);
    bool set_socket_priority(int priority);

    int fd() const { return fd_; }

private:
    bool do_connect(uint32_t timeout_ms);

    int  fd_;
    int  epoll_fd_;
    std::atomic<bool> connected_;
    std::string host_;
    uint16_t    port_{0};
};

class RetrySocket {
public:
    RetrySocket(const std::string& host, uint16_t port,
                int max_retries = 10, uint32_t base_backoff_ms = 100);

    bool connect_with_retry();
    MarketDataSocket& socket() { return sock_; }

private:
    MarketDataSocket sock_;
    std::string host_;
    uint16_t    port_;
    int         max_retries_;
    uint32_t    base_backoff_ms_;
};
