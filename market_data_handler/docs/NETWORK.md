# NETWORK.md – Socket Implementation Details

## 1. Server-Side Design

### 1a. Multi-Client epoll Handling

The server uses a single `epoll` instance managing both the listening socket and all connected client fds:

```
epoll_fd
  ├── server_fd  (EPOLLIN – new connections)
  ├── client_fd1 (EPOLLIN | EPOLLET – client data)
  ├── client_fd2 (EPOLLIN | EPOLLET)
  └── ...
```

The event loop alternates between:
1. Calling `epoll_wait(timeout=1ms)` to process new connections and client disconnections.
2. Generating and broadcasting market ticks based on elapsed time vs. target rate.

This single-threaded design avoids locking and context switching entirely at the cost of a small latency jitter (~1ms) from the epoll timeout. At 100K msg/s this is acceptable; at 500K msg/s a tighter loop (timeout=0 with CPU spin) would be used.

### 1b. Broadcast Strategy

Broadcasting iterates over an `unordered_set<int>` of client fds and calls `send()` on each with `MSG_DONTWAIT | MSG_NOSIGNAL`. Key points:

- `MSG_NOSIGNAL`: prevents `SIGPIPE` on broken connections (we handle errors via return code).
- Non-blocking: if the kernel send buffer is full, we get `EAGAIN` immediately rather than blocking all other clients.
- A separate "slow client" set tracks fd's that returned EAGAIN for monitoring.

An alternative is a fan-out thread per client, but that adds context-switch overhead proportional to client count. For ≤100 clients, single-threaded broadcast is faster.

### 1c. Slow Client Detection and Handling

When `send()` returns `EAGAIN`, the client's TCP send buffer is full, indicating it cannot keep up with the tick rate. Options:

| Strategy | Tradeoff |
|----------|----------|
| **Drop message** (current) | Lowest server overhead; client sees gaps |
| **Queue in userspace** | Increases latency for other clients; memory risk |
| **Disconnect slow client** | Harsh but prevents resource exhaustion |
| **Throttle per-client rate** | Complex; requires per-client state |

The current implementation logs slow clients and skips that client's delivery for the current tick, preserving throughput for fast clients.

### 1d. Connection State Management

Each client fd is tracked in `unordered_set<int> clients_`. On disconnect (`recv` returns 0 or EPOLLHUP/EPOLLERR), the fd is removed from epoll and closed. The set operations are O(1) average, keeping broadcast overhead low even at 1000+ clients.

---

## 2. Client-Side Design

### 2a. Socket Programming Decisions

```cpp
fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
```

- `SOCK_NONBLOCK`: create non-blocking from the start (avoids `fcntl` round-trip).
- `TCP_NODELAY`: disable Nagle's algorithm. Nagle coalesces small writes into larger segments, adding up to 40ms latency for ACK-delayed networks. For market data every microsecond counts.
- `SO_RCVBUF = 4MB`: the kernel receive buffer absorbs bursts during userspace processing. At 100K × 44 bytes/s = 4.4 MB/s, a 4MB buffer provides ~1 second of absorption before kernel starts dropping.
- `SO_PRIORITY`: optional; on co-location hardware, raising socket priority reduces queueing delay in the NIC TX queue.

### 2b. Why epoll Over select/poll?

| Mechanism | Complexity | Max FDs | FD set copying |
|-----------|-----------|---------|----------------|
| `select` | O(n) scan | 1024 (FD_SETSIZE) | Yes – every call |
| `poll` | O(n) scan | Unlimited | Yes – array copy |
| `epoll` | O(1) per event | Millions | No – kernel maintains set |

For a single-connection feed handler, the performance gap is small. The choice of epoll is made because:
1. The same code pattern scales to multi-symbol, multi-exchange architectures.
2. `EPOLLRDHUP` provides half-close detection not available in `select`/`poll`.
3. Edge-triggered mode (`EPOLLET`) is only available in epoll.

### 2c. Edge-Triggered vs Level-Triggered

**Level-triggered (LT)**: epoll_wait returns as long as the fd has data. Risk: if the application processes only part of the available data, it is woken again immediately — causing busy-looping.

**Edge-triggered (ET)**: epoll_wait returns only when new data arrives. The application **must** call `recv()` in a loop until `EAGAIN`. This avoids re-entry overhead and is the correct model for a feed handler that already drains the socket fully.

```cpp
// Correct ET drain loop:
while (true) {
    ssize_t n = recv(fd, buf, BUF_SIZE, MSG_DONTWAIT);
    if (n > 0)  { process(buf, n); continue; }
    if (n == 0) { handle_disconnect(); break; }
    if (errno == EAGAIN) break;   // kernel buffer drained
    handle_error(); break;
}
```

### 2d. Non-Blocking I/O Patterns

The connect sequence uses epoll to detect completion:
1. `connect()` on a non-blocking socket returns `-1` with `errno == EINPROGRESS`.
2. Register fd for `EPOLLOUT`.
3. `epoll_wait(timeout=5000ms)`.
4. On EPOLLOUT, check `SO_ERROR` with `getsockopt` to confirm success.

This allows connection timeout without blocking the calling thread.

---

## 3. TCP Stream Handling

### 3a. Message Boundary Detection

TCP is a stream protocol — it does not preserve write boundaries. A single `recv()` call may return:
- A partial message (first N bytes of a 44-byte QuoteMsg)
- Exactly one message
- Multiple messages concatenated
- Multiple messages plus a partial next message

The `Parser` handles all cases via a reassembly buffer. The detection algorithm:

```
1. Need at least sizeof(MsgHeader) = 16 bytes to know message type
2. Look up expected total size from msg_type (16, 32, or 44 bytes)
3. If buf_used >= expected: consume message, advance offset
4. Else: wait for more data
```

### 3b. Partial Read Buffering Strategy

```
┌──────────────────────────────────────┐
│            Parser internal buffer    │
│  [consumed][complete msg][partial..] │
└──────────────────────────────────────┘
                   ▲
           offset pointer advances
           then memmove at end:
           [partial..][free space      ]
```

After each `feed()` call, `memmove` shifts remaining bytes to the front. At 100K msg/s × 44 bytes = 4.4 MB/s throughput, the memmove cost is ~4.4 MB/s × ~1ns/byte = ~4.4ms CPU/s — negligible.

### 3c. Buffer Sizing Calculations

Receive buffer: 64 KB stack allocation.

At 100K msg/s × 44 bytes = 4.4 MB/s → 4.4 KB/ms. A 64 KB buffer holds ~14ms of data, giving the application ample time to drain between epoll_wait wakeups.

Parser reassembly buffer: 128 KB — must be at least `2 × MAX_MSG_SIZE` (44 bytes), but 128 KB accommodates bursts of unfed data without resizing.

---

## 4. Connection Management

### 4a. Connection State Machine

```
DISCONNECTED ──connect()──▶ CONNECTING
                                │
                         epoll EPOLLOUT
                                │
                       getsockopt SO_ERROR
                         ┌──────┴──────┐
                      error=0        error≠0
                         │              │
                      CONNECTED    DISCONNECTED
                         │
              recv()=0 / EPOLLERR / EPOLLHUP / EPOLLRDHUP
                         │
                    DISCONNECTED ──retry──▶ CONNECTING
```

### 4b. Retry Logic and Backoff Algorithm

```
attempt=0, backoff=100ms
loop:
    try connect()
    if success: return
    sleep(backoff)
    backoff = min(backoff * 2, 30000)  // exponential, cap 30s
    attempt++
    if attempt >= max_retries: fail
```

Exponential backoff prevents thundering herd when many clients reconnect simultaneously after a server restart.

### 4c. Heartbeat Mechanism

The server sends a `HeartbeatMsg` every 1 second. The feed handler can detect silent connection drops (no FIN/RST from the network) by monitoring heartbeat gaps. A missing heartbeat after 3 seconds would trigger reconnection. (Full heartbeat timeout logic is left as an extension; the infrastructure is in place via `MSG_HEARTBEAT`.)

---

## 5. Error Handling

### 5a. Network Errors

| Error | Handling |
|-------|----------|
| `EPIPE` | Suppressed with `MSG_NOSIGNAL`; `send()` returns -1 |
| `ECONNRESET` | Client removed from epoll and closed |
| `EAGAIN`/`EWOULDBLOCK` | Normal for non-blocking sockets; retry later |
| `EINTR` | Re-enter `recv`/`send` (not shown; assume no signals in hot path) |
| `EBADF` | Should not occur; indicates programming error |

### 5b. Application-Level Errors

| Error | Handling |
|-------|----------|
| Bad checksum | Message discarded; counter incremented |
| Sequence gap | Logged; gap count incremented; processing continues |
| Unknown message type | Scanner advances one byte (recovery heuristic) |
| Buffer overflow in parser | State reset; log warning |

### 5c. Recovery Strategies

- **Parser reset on reconnect**: ensures no partial messages from the previous connection contaminate the new stream.
- **Sequence gap tolerance**: gaps are logged but not fatal. In a real system, a gap might trigger a snapshot request to resync state.
- **Bad checksum skip**: assumes the next message header is at `offset + expected_size`. If the corruption is severe, the unknown-type fallback will eventually re-synchronise.
