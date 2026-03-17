#include "client_manager.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>


ClientManager::ClientManager(int epoll_fd)
    : epoll_fd_(epoll_fd)
    , total_connected_(0)
    , total_disconnected_(0)
    , total_slow_(0)
{}

ClientManager::~ClientManager() {
    for (auto& [fd, info] : clients_) {
        ::close(fd);
    }
}

bool ClientManager::add(int fd, const std::string& remote_addr) {
    int flag = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    int sndbuf = SEND_BUF_SIZE;
    ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    epoll_event ev{};
    ev.events  = EPOLLIN | EPOLLET | EPOLLRDHUP;
    ev.data.fd = fd;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        std::cerr << "[ClientManager] epoll_ctl ADD failed: " << strerror(errno) << "\n";
        ::close(fd);
        return false;
    }

    ClientInfo info{};
    info.fd          = fd;
    info.remote_addr = remote_addr;
    info.connect_time = std::chrono::steady_clock::now();
    info.bytes_sent  = 0;
    info.msgs_sent   = 0;
    info.is_slow     = false;

    clients_.emplace(fd, info);
    total_connected_++;

    std::cout << "[ClientManager] Client connected fd=" << fd
              << " addr=" << remote_addr
              << " total=" << clients_.size() << "\n";
    return true;
}

void ClientManager::remove(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;

    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - it->second.connect_time).count();

    std::cout << "[ClientManager] Client disconnected fd=" << fd
              << " addr=" << it->second.remote_addr
              << " uptime=" << elapsed << "s"
              << " msgs_sent=" << it->second.msgs_sent << "\n";

    clients_.erase(it);
    total_disconnected_++;
}

void ClientManager::broadcast(const void* data, size_t len) {
    std::vector<int> to_remove;

    for (auto& [fd, info] : clients_) {
        if (!send_to(fd, data, len)) {
            to_remove.push_back(fd);
        }
    }

    for (int fd : to_remove) remove(fd);
}

bool ClientManager::send_to(int fd, const void* data, size_t len) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return false;

    const char* ptr = reinterpret_cast<const char*>(data);
    size_t remaining = len;
    size_t sent_total = 0;

    while (remaining > 0) {
        ssize_t r = ::send(fd, ptr + sent_total, remaining,
                           MSG_DONTWAIT | MSG_NOSIGNAL);
        if (r > 0) {
            sent_total  += r;
            remaining   -= r;
        } else if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!it->second.is_slow) {
                it->second.is_slow = true;
                total_slow_++;
                std::cerr << "[ClientManager] Slow consumer fd=" << fd << "\n";
            }
            break;
        } else {
            return false;
        }
    }

    it->second.bytes_sent += sent_total;
    it->second.msgs_sent++;
    if (sent_total == len) it->second.is_slow = false; 
    return true;
}

size_t ClientManager::count() const {
    return clients_.size();
}

void ClientManager::print_stats() const {
    std::cout << "[ClientManager] Stats:"
              << " connected="    << clients_.size()
              << " total_conn="   << total_connected_
              << " total_disc="   << total_disconnected_
              << " slow_events="  << total_slow_ << "\n";

    for (const auto& [fd, info] : clients_) {
        std::cout << "  fd=" << fd
                  << " addr=" << info.remote_addr
                  << " msgs=" << info.msgs_sent
                  << " bytes=" << info.bytes_sent
                  << (info.is_slow ? " [SLOW]" : "") << "\n";
    }
}
