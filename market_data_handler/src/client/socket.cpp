#include "socket.h"
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
#include <thread>

MarketDataSocket::MarketDataSocket()
    : fd_(-1), epoll_fd_(-1), connected_(false) {}

MarketDataSocket::~MarketDataSocket() { disconnect(); }

bool MarketDataSocket::connect(const std::string& host, uint16_t port,
                               uint32_t timeout_ms) {
    host_ = host;
    port_ = port;
    return do_connect(timeout_ms);
}

bool MarketDataSocket::do_connect(uint32_t timeout_ms) {
    // Close any existing fd
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }

    fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd_ < 0) { perror("socket"); return false; }

    // Socket options
    set_tcp_nodelay(true);
    set_recv_buffer_size(4 * 1024 * 1024);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port_);
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "[Socket] Invalid address: " << host_ << "\n";
        ::close(fd_); fd_ = -1;
        return false;
    }

    int r = ::connect(fd_, (sockaddr*)&addr, sizeof(addr));
    if (r < 0 && errno != EINPROGRESS) {
        perror("connect");
        ::close(fd_); fd_ = -1;
        return false;
    }

    // Wait for connect with epoll
    if (epoll_fd_ < 0) {
        epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0) { perror("epoll_create1"); return false; }
    }

    epoll_event ev{};
    ev.events  = EPOLLOUT | EPOLLET;
    ev.data.fd = fd_;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd_, &ev);

    epoll_event events[1];
    int n = ::epoll_wait(epoll_fd_, events, 1, static_cast<int>(timeout_ms));
    if (n <= 0) {
        std::cerr << "[Socket] Connect timeout\n";
        ::close(fd_); fd_ = -1;
        return false;
    }

    // Check for connect error
    int err = 0;
    socklen_t len = sizeof(err);
    ::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err) {
        std::cerr << "[Socket] Connect error: " << strerror(err) << "\n";
        ::close(fd_); fd_ = -1;
        return false;
    }

    // Re-register for reads with edge-triggered
    ev.events  = EPOLLIN | EPOLLET | EPOLLRDHUP;
    ev.data.fd = fd_;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd_, &ev);

    connected_.store(true, std::memory_order_release);
    std::cout << "[Socket] Connected to " << host_ << ":" << port_ << "\n";
    return true;
}

ssize_t MarketDataSocket::receive(void* buffer, size_t max_len) {
    if (!connected_.load(std::memory_order_acquire)) return -1;

    ssize_t r = ::recv(fd_, buffer, max_len, MSG_DONTWAIT);
    if (r > 0) return r;
    if (r == 0) {
        // FIN received – graceful close
        std::cout << "[Socket] Server closed connection\n";
        connected_.store(false, std::memory_order_release);
        return -1;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;  
    // Other error
    perror("[Socket] recv");
    connected_.store(false, std::memory_order_release);
    return -1;
}

bool MarketDataSocket::send_subscription(const std::vector<uint16_t>& symbol_ids) {
    if (!connected_.load()) return false;

    size_t total = 3 + symbol_ids.size() * 2; 
    std::vector<uint8_t> buf(total);
    buf[0] = CMD_SUBSCRIBE;
    uint16_t cnt = static_cast<uint16_t>(symbol_ids.size());
    std::memcpy(&buf[1], &cnt, 2);
    std::memcpy(&buf[3], symbol_ids.data(), symbol_ids.size() * 2);

    ssize_t sent = ::send(fd_, buf.data(), total, MSG_NOSIGNAL);
    return sent == static_cast<ssize_t>(total);
}

int MarketDataSocket::wait_for_data(int timeout_ms) {
    epoll_event events[1];
    return ::epoll_wait(epoll_fd_, events, 1, timeout_ms);
}

bool MarketDataSocket::is_connected() const {
    return connected_.load(std::memory_order_acquire);
}

void MarketDataSocket::disconnect() {
    connected_.store(false, std::memory_order_release);
    if (fd_ >= 0)     { ::close(fd_);      fd_ = -1; }
    if (epoll_fd_ >= 0) { ::close(epoll_fd_); epoll_fd_ = -1; }
}

bool MarketDataSocket::set_tcp_nodelay(bool enable) {
    int v = enable ? 1 : 0;
    return ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v)) == 0;
}

bool MarketDataSocket::set_recv_buffer_size(size_t bytes) {
    int sz = static_cast<int>(bytes);
    return ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz)) == 0;
}

bool MarketDataSocket::set_socket_priority(int priority) {
    return ::setsockopt(fd_, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority)) == 0;
}

RetrySocket::RetrySocket(const std::string& host, uint16_t port,
                         int max_retries, uint32_t base_backoff_ms)
    : host_(host), port_(port)
    , max_retries_(max_retries), base_backoff_ms_(base_backoff_ms)
{}

bool RetrySocket::connect_with_retry() {
    int attempt = 0;
    uint32_t backoff = base_backoff_ms_;
    while (attempt < max_retries_) {
        if (sock_.connect(host_, port_)) return true;
        ++attempt;
        std::cerr << "[RetrySocket] Attempt " << attempt
                  << " failed, retrying in " << backoff << "ms\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
        backoff = std::min(backoff * 2, 30000u);  
    }
    return false;
}
