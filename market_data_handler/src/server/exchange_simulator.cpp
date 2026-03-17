#include "exchange_simulator.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <random>

// ─── Helpers ──────────────────────────────────────────────────────────────────
static void set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) throw std::runtime_error("fcntl F_GETFL");
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        throw std::runtime_error("fcntl F_SETFL");
}

static void set_tcp_nodelay(int fd) {
    int on = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

// ─── ExchangeSimulator ────────────────────────────────────────────────────────
ExchangeSimulator::ExchangeSimulator(uint16_t port, size_t num_symbols)
    : port_(port)
    , gen_(num_symbols)
    , running_(false)
    , tick_rate_(100000)
    , fault_injection_(false)
    , seq_no_(1)
{
    symbol_count_ = num_symbols;
}

ExchangeSimulator::~ExchangeSimulator() {
    running_.store(false);
    if (server_fd_ >= 0) ::close(server_fd_);
    if (epoll_fd_ >= 0) ::close(epoll_fd_);
}

void ExchangeSimulator::set_tick_rate(uint32_t tps) {
    tick_rate_.store(tps, std::memory_order_relaxed);
}

void ExchangeSimulator::enable_fault_injection(bool en) {
    fault_injection_.store(en, std::memory_order_relaxed);
}

void ExchangeSimulator::start() {
    // Create listen socket
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) throw std::runtime_error("socket()");

    int reuse = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (::bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0)
        throw std::runtime_error(std::string("bind(): ") + strerror(errno));

    if (::listen(server_fd_, 128) < 0)
        throw std::runtime_error("listen()");

    set_nonblocking(server_fd_);

    // Create epoll instance
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) throw std::runtime_error("epoll_create1()");

    epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = server_fd_;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &ev);

    running_.store(true);
    std::cout << "[Server] Listening on port " << port_
              << " | symbols=" << symbol_count_
              << " | tick_rate=" << tick_rate_.load() << "/s\n";
}

void ExchangeSimulator::run() {
    static constexpr int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    auto last_tick = std::chrono::steady_clock::now();
    uint64_t ticks_this_second = 0;

    while (running_.load(std::memory_order_relaxed)) {
        // Poll for I/O events (1ms timeout so we can generate ticks)
        int n = ::epoll_wait(epoll_fd_, events, MAX_EVENTS, 1);
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == server_fd_) {
                handle_new_connection();
            } else if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                handle_client_disconnect(fd);
            } else if (events[i].events & EPOLLIN) {
                // Read subscription or any client data (ignore body for now)
                char buf[512];
                ssize_t r = ::recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
                if (r <= 0) handle_client_disconnect(fd);
            }
        }

        // Tick generation
        auto now = std::chrono::steady_clock::now();
        double elapsed_s = std::chrono::duration<double>(now - last_tick).count();
        uint32_t rate = tick_rate_.load(std::memory_order_relaxed);
        uint64_t target_ticks = static_cast<uint64_t>(elapsed_s * rate);

        if (ticks_this_second < target_ticks) {
            generate_and_broadcast();
            ticks_this_second++;
        }

        // Reset counter every second
        if (elapsed_s >= 1.0) {
            last_tick = now;
            ticks_this_second = 0;
            // Heartbeat every second
            if (!clients_.empty()) {
                auto hb = gen_.make_heartbeat(seq_no_++);
                broadcast_message(&hb, sizeof(hb));
            }
        }
    }
}

void ExchangeSimulator::handle_new_connection() {
    sockaddr_in cli_addr{};
    socklen_t len = sizeof(cli_addr);
    int cli_fd = ::accept4(server_fd_, (sockaddr*)&cli_addr, &len, SOCK_NONBLOCK);
    if (cli_fd < 0) return;

    set_tcp_nodelay(cli_fd);

    // Set send buffer to 4 MB
    int sndbuf = 4 * 1024 * 1024;
    ::setsockopt(cli_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    epoll_event ev{};
    ev.events  = EPOLLIN | EPOLLET;  // edge-triggered
    ev.data.fd = cli_fd;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, cli_fd, &ev);

    clients_.insert(cli_fd);
    std::cout << "[Server] Client connected fd=" << cli_fd
              << " from " << inet_ntoa(cli_addr.sin_addr) << "\n";
}

void ExchangeSimulator::handle_client_disconnect(int fd) {
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    clients_.erase(fd);
    slow_clients_.erase(fd);
    std::cout << "[Server] Client disconnected fd=" << fd << "\n";
}

void ExchangeSimulator::generate_and_broadcast() {
    if (clients_.empty()) return;

    // Round-robin over symbols; 70% quotes, 30% trades
    uint16_t sym = static_cast<uint16_t>(seq_no_ % symbol_count_);

    // Fault injection: skip message to simulate sequence gap
    if (fault_injection_.load() && (seq_no_ % 100) == 0) {
        seq_no_++;  // gap
    }

    uint32_t seq = seq_no_++;
    bool is_trade = (seq % 10) < 3;  // 30% trades

    if (is_trade) {
        TradeMsg msg = gen_.make_trade(sym, seq);
        broadcast_message(&msg, sizeof(msg));
    } else {
        QuoteMsg msg = gen_.make_quote(sym, seq);
        broadcast_message(&msg, sizeof(msg));
    }
}

void ExchangeSimulator::broadcast_message(const void* data, size_t len) {
    std::vector<int> to_drop;
    for (int fd : clients_) {
        ssize_t sent = 0;
        const char* ptr = reinterpret_cast<const char*>(data);
        size_t remaining = len;

        while (remaining > 0) {
            ssize_t r = ::send(fd, ptr + sent, remaining, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (r > 0) {
                sent += r;
                remaining -= r;
            } else if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // Slow consumer: track and potentially drop
                slow_clients_.insert(fd);
                break;
            } else {
                to_drop.push_back(fd);
                break;
            }
        }
    }
    for (int fd : to_drop) handle_client_disconnect(fd);
}
