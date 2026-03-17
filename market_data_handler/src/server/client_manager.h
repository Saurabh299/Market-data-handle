#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>
#include <chrono>

class ClientManager {
public:
    static constexpr int SEND_BUF_SIZE = 4 * 1024 * 1024; 

    explicit ClientManager(int epoll_fd);
    ~ClientManager();

    bool add(int fd, const std::string& remote_addr);

    void remove(int fd);

    void broadcast(const void* data, size_t len);

    bool send_to(int fd, const void* data, size_t len);

    size_t count() const;
    bool   empty() const { return clients_.empty(); }

    void print_stats() const;

    uint64_t total_connected()    const { return total_connected_; }
    uint64_t total_disconnected() const { return total_disconnected_; }

private:
    struct ClientInfo {
        int         fd;
        std::string remote_addr;
        std::chrono::steady_clock::time_point connect_time;
        uint64_t    bytes_sent;
        uint64_t    msgs_sent;
        bool        is_slow;
    };

    int epoll_fd_;
    std::unordered_map<int, ClientInfo> clients_;

    uint64_t total_connected_;
    uint64_t total_disconnected_;
    uint64_t total_slow_;
};
