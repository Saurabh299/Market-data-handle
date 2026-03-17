# DESIGN.md – Architecture & Design Decisions

## 1. System Architecture

### 1a. Client-Server Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Exchange Simulator (Server)                       │
│                                                                     │
│  ┌─────────────┐   ┌──────────────┐   ┌──────────────────────────┐ │
│  │ epoll loop  │──▶│ TickGenerator │──▶│  broadcast_message()     │ │
│  │ (accept/IO) │   │  (GBM, 100   │   │  iterate client_fds set  │ │
│  └─────────────┘   │   symbols)   │   │  non-blocking send()     │ │
│                    └──────────────┘   └──────────────────────────┘ │
└───────────────────────────────┬─────────────────────────────────────┘
                                │  Binary Protocol over TCP (44 bytes max)
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      Feed Handler (Client)                          │
│                                                                     │
│  ┌──────────────┐   ┌──────────┐   ┌─────────────┐   ┌──────────┐ │
│  │MarketDataSock│──▶│  Parser  │──▶│ SymbolCache │──▶│Visualizer│ │
│  │(epoll ET)    │   │(seqlock  │   │(seqlock     │   │(500ms    │ │
│  │recv→buffer   │   │ reassembly│   │ write path) │   │ refresh) │ │
│  └──────────────┘   └──────────┘   └─────────────┘   └──────────┘ │
│                                          │                          │
│                                    ┌─────┴────────┐                 │
│                                    │LatencyTracker│                 │
│                                    │(atomic histo)│                 │
│                                    └──────────────┘                 │
└─────────────────────────────────────────────────────────────────────┘
```

### 1b. Thread Model

**Server (single-threaded event loop):**
- One thread runs `epoll_wait` for accept + I/O readiness
- Tick generation is interleaved with epoll polling (1ms timeout)
- Broadcast is synchronous within the event loop; slow consumers are detected via EAGAIN

**Client (two threads):**
- **Network/Parse thread**: `epoll_wait` → `recv` → `Parser::feed` → `SymbolCache::update*` → `LatencyTracker::record`
- **Visualizer thread**: wakes every 500ms, reads snapshot via `SymbolCache::get_snapshot` (lock-free), renders ANSI output

### 1c. Data Flow: Tick Generation → Terminal

```
GBM step (σ, μ, dW)
    │
    ▼
QuoteMsg / TradeMsg  (binary, packed, with checksum)
    │   TCP stream
    ▼
recv() into 64 KB stack buffer
    │
    ▼
Parser::feed()  ← reassembly across fragmented packets
    │            ← checksum verification
    │            ← sequence gap detection
    ▼
Callback (on_quote / on_trade)
    │
    ▼
SymbolCache::update_*()   ← seqlock write (odd→write→even)
    │
    ▼  (separate thread, 500ms poll)
SymbolCache::get_snapshot()  ← seqlock read (spin on odd version)
    │
    ▼
ANSI terminal render (single buffered write to stdout)
```

---

## 2. Geometric Brownian Motion

See [GBM.md](GBM.md) for full mathematical treatment.

**Parameter choices:**
| Parameter | Value | Rationale |
|-----------|-------|-----------|
| μ (drift) | 0.0 / ±0.05 | Neutral or ±5% annualised for realism |
| σ (volatility) | 0.01–0.06 per symbol | Covers blue-chip to small-cap range |
| dt | 0.001 | 1ms time step matches tick rate |
| Initial price | ₹100–₹5000 | Covers NSE universe |

---

## 3. Network Layer Design

### 3a. Server – epoll multi-client

The server uses a single epoll instance. The listening socket is registered for `EPOLLIN`. When `accept()` returns a new fd, it is registered with `EPOLLIN | EPOLLET` (edge-triggered). Client sockets use `SO_SNDBUF = 4MB` to tolerate burst spikes.

### 3b. Client – epoll event loop

The client creates one epoll fd. The TCP socket is registered with `EPOLLIN | EPOLLET | EPOLLRDHUP`. Edge-triggered mode is used so that each readiness notification drives a `recv` loop until `EAGAIN` — this is critical for high-rate feeds where level-triggered mode would cause redundant epoll_wait wakeups.

### 3c. Buffer Management for TCP Streams

TCP is a byte stream; message boundaries are not preserved. The `Parser` maintains an internal 128 KB reassembly buffer. Each `feed()` call appends incoming bytes, then walks forward consuming complete messages. Residual bytes are `memmove`'d to the front. This avoids dynamic allocation on the hot path.

### 3d. Reconnection Logic

`RetrySocket` implements exponential backoff starting at 100ms, doubling each attempt, capped at 30s, for up to 10 attempts. The reconnect is triggered when `recv()` returns 0 (FIN) or a hard error. The parser state is reset on reconnect to avoid processing stale partial messages.

### 3e. Flow Control

The server calls `send()` with `MSG_DONTWAIT`. If `EAGAIN` is returned, the client fd is tagged as a "slow consumer". In production this would trigger backpressure (e.g., reduce symbol subscription) or disconnect. Currently the server tracks `slow_clients_` and logs them without disconnecting, to preserve data delivery.

---

## 4. Memory Management Strategy

### 4a. Buffer Lifecycle

Receive buffers are allocated once at startup as `std::vector<uint8_t>(65536)` per connection. The parser's internal reassembly buffer is stack-allocated (128 KB, cache-line aligned). No heap allocation occurs on the hot path.

### 4b. Allocation Patterns

- **Server tick generation**: `TickGenerator` pre-allocates `SymbolState` vector at construction. Each `make_quote` / `make_trade` writes into a stack-local struct and returns by value (RVO applies).
- **Parser**: fixed-size internal array, no `new`/`delete` on hot path.
- **SymbolCache**: fixed-size array of 500 `MarketState` structs, zero-initialized.

### 4c. Alignment and Cache Considerations

`MarketState` is `alignas(64)` to start on a cache line boundary. This prevents false sharing between adjacent symbol states when the writer updates symbol N while a reader accesses symbol N+1.

### 4d. Memory Pool

`MemoryPool<BlockSize, PoolSize>` implements a lock-free Treiber stack of fixed-size blocks. It is provided for future use with multiple concurrent connections but the single-connection feed handler uses a simpler stack-allocated buffer for lower latency (avoids CAS).

---

## 5. Concurrency Model

### 5a. Lock-Free Techniques

**Seqlock** in `SymbolCache`:
- Writer increments `version` to odd before writing, to even after.
- Reader spins while `version` is odd; re-reads if version changed between the two loads.
- No mutex needed; the single-writer assumption eliminates writer–writer contention.

**Atomic histogram** in `LatencyTracker`:
- Each bucket is an `std::atomic<uint64_t>` incremented with `fetch_add(…, relaxed)`.
- Min/max use `compare_exchange_weak` loops.
- Stats computation does not need to be atomic-consistent; approximate percentiles are acceptable for telemetry.

### 5b. Memory Ordering Choices

| Operation | Ordering | Reason |
|-----------|----------|--------|
| Seqlock begin_write | `release` | Ensure prior stores are visible before version odd |
| Seqlock end_write | `release` + fence | Ensure data stores complete before version even |
| Seqlock read version | `acquire` | Synchronise with writer's release |
| Histogram bucket increment | `relaxed` | Order doesn't matter; only the atomic increment itself |
| `connected_` flag | `release` on write, `acquire` on read | Ensures socket state is visible across threads |

### 5c. Single-Writer Multiple-Reader Pattern

One network thread writes to `SymbolCache`; one visualizer thread reads. The seqlock is ideal: zero overhead for the writer, and the reader only retries on the rare occasion it observes a write in progress. With 500ms visualization intervals and 100K+ writes/s, retries are negligible.

---

## 6. Visualization Design

### 6a. Update Strategy

The visualizer runs a dedicated thread that sleeps 500ms between renders. It does **not** use a condition variable or event queue — polling is simpler and the 500ms granularity is coarse enough that CPU cost is negligible (< 0.1%).

### 6b. ANSI Escape Codes vs ncurses

Raw ANSI codes are used (`\033[2J\033[H` to clear, colour codes for green/red). This avoids the ncurses dependency and initialization overhead. The tradeoff is that terminal resize is not perfectly handled; a `SIGWINCH` handler could be added to re-query `ioctl(TIOCGWINSZ)`.

### 6c. Statistics Without Blocking

All statistics (message count, latency percentiles) are read from atomic variables and the lock-free `LatencyTracker`. The render function builds a complete output string in a `std::ostringstream` before issuing a single `std::cout` write, minimising the time the terminal is in a partially-updated state.

---

## 7. Performance Optimization

### 7a. Hot Path Identification

The hot path is: `recv()` → `Parser::feed()` → `SymbolCache::update_*()` → `LatencyTracker::record()`.

All four operations are designed to execute without heap allocation, mutex locks, or system calls (beyond `recv` itself).

### 7b. Cache Optimization Techniques

- `alignas(64)` on `MarketState` and parser buffer prevents false sharing.
- `SymbolCache::states_` is a flat array (no pointer indirection), so sequential symbol access is cache-friendly.
- `TickGenerator::SymbolState` vector is pre-sized to avoid reallocation.

### 7c. False Sharing Prevention

Each `MarketState` is padded to at least one cache line. `LatencyTracker` buckets are stored in a `std::array` of atomics; since buckets are written independently by any thread, each bucket ideally occupies its own cache line. With 64-byte cache lines and 8-byte atomics, up to 8 buckets share a line — acceptable for telemetry-grade instrumentation.

### 7d. System Call Minimization

- `TCP_NODELAY` disables Nagle's algorithm, eliminating 40ms batching delays.
- `SO_RCVBUF = 4MB` reduces the frequency of kernel→userspace copies by allowing large bursts to accumulate.
- Edge-triggered epoll avoids re-entering `epoll_wait` while data is still available in the kernel buffer.
- `MSG_DONTWAIT` on `recv` avoids blocking; the event loop can drain multiple messages per wakeup.
