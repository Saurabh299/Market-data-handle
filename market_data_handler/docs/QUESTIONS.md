# QUESTIONS.md – Answers to Critical Thinking Questions

---

## Section 1: Exchange Simulator

**Q1. How do you efficiently broadcast to multiple clients without blocking?**

Use `send()` with `MSG_DONTWAIT | MSG_NOSIGNAL` on each client fd. The call returns immediately if the kernel send buffer is full (`EAGAIN`), so a slow client never blocks the broadcast loop. The iteration over client fds is O(n) but each `send()` is a non-blocking system call — at 100 clients and 44-byte messages the loop completes in microseconds.

For >1000 clients, a more scalable approach is to use `sendfile()` or `io_uring` with a shared memory buffer, writing once and referencing it from multiple fd operations.

**Q2. What happens when a client's TCP send buffer fills up?**

`send()` returns -1 with `errno == EAGAIN`. The message is not delivered to that client. Options: (a) tag the client as slow and skip future ticks until its buffer drains, (b) maintain a per-client userspace queue and flush it when the fd becomes writable again (requires `EPOLLOUT` monitoring), or (c) disconnect the client to protect other clients. The current implementation takes option (a): tag and skip.

**Q3. How do you ensure fair distribution when some clients are slower?**

Fairness is not strictly required for a market data feed — all clients should receive the same data, not get compensatory priority. The correct approach is to drop messages to slow clients (market data is time-sensitive; stale data is worse than no data) and optionally send them a gap notification. A real exchange (NSE/BSE) disconnects clients that cannot keep up with the feed rate.

**Q4. How would you handle 1000+ concurrent client connections?**

- Use `epoll` (already done) — scales to millions of fds.
- Replace `unordered_set` iteration for broadcast with a sorted `vector<int>` for better cache locality.
- Use `io_uring` (Linux 5.1+) to batch multiple `send()` calls into a single system call via a submission ring, reducing syscall overhead from O(n) to O(1) amortised.
- Consider multicast UDP (within a co-location LAN) — send once, N clients receive — eliminating the per-client send loop entirely. NSE's co-lo actually uses multicast for this reason.

---

## Section 2: TCP Client Socket

**Q1. Why use epoll edge-triggered instead of level-triggered for feed handler?**

At 100K+ msg/s, data arrives continuously. Level-triggered (LT) would return from `epoll_wait` on every call while data is in the buffer — essentially busy-looping. Edge-triggered (ET) fires only on new data arriving, so the application drains the socket to `EAGAIN` in a tight `recv()` loop, then sleeps in `epoll_wait` until more data arrives. This is more CPU-efficient and avoids redundant kernel-to-userspace transitions.

**Q2. How do you handle the case where `recv()` returns EAGAIN/EWOULDBLOCK?**

This is the expected exit condition from the ET drain loop. Return 0 to the caller, indicating "no more data right now." The event loop then calls `epoll_wait` again to wait for the next readiness notification. No special error handling is needed — this is normal non-blocking I/O behaviour.

**Q3. What happens if the kernel receive buffer fills up?**

The sending side's TCP stack sees its window close (receive window advertisement drops to 0). The sender stops transmitting until the window reopens. This is TCP's built-in flow control. The consequence for the feed handler is that messages queue in the server's send buffer; if that also fills, the server gets `EAGAIN` and drops or queues the message (see Section 1 Q2). The feed handler should size `SO_RCVBUF` large enough to absorb processing jitter.

**Q4. How do you detect a silent connection drop (no FIN/RST)?**

TCP keepalive (`SO_KEEPALIVE`, `TCP_KEEPIDLE`, `TCP_KEEPINTVL`, `TCP_KEEPCNT`) sends probe packets after a period of inactivity. Set `TCP_KEEPIDLE=5s`, `TCP_KEEPINTVL=1s`, `TCP_KEEPCNT=3` to detect a dead connection within ~8 seconds.

Application-level heartbeats (the server's `HeartbeatMsg` every 1s) are faster and more reliable than TCP keepalives because they traverse the same application code path. If no heartbeat is received for 3 seconds, declare the connection dead and reconnect.

**Q5. Should reconnection logic be in the same thread or separate?**

For a simple single-connection feed handler: same thread is fine. The connect attempt blocks (with timeout) before re-entering the receive loop. For a multi-exchange handler connecting to multiple feeds simultaneously, a separate reconnection thread (or `io_uring` async connect) prevents one feed's reconnect from stalling others.

---

## Section 3: Binary Protocol Parser

**Q1. How do you buffer incomplete messages across multiple recv() calls efficiently?**

Maintain a fixed-size (128 KB) statically-allocated reassembly buffer with a `buf_used` cursor. Each `feed()` call appends new bytes with `memcpy`, then walks forward consuming complete messages. After consuming, `memmove` the remainder to the front. This approach: (a) has no heap allocation, (b) handles arbitrary fragmentation, (c) is cache-friendly (single contiguous buffer), and (d) is O(N) in bytes processed.

**Q2. What happens when you detect a sequence gap — drop or request retransmission?**

For a **market data feed**, drop and continue. Market data is time-critical; by the time a retransmission arrives, the state it described is stale. The correct response to a gap is: log it, increment the gap counter, update the last-seen sequence, and continue processing from the next received message. If the gap indicates a serious desynchronisation, trigger a full snapshot request to the server.

Contrast with **order entry** (FIX protocol): there, retransmission is mandatory because a missed order acknowledgement has financial consequences.

**Q3. How would you handle messages arriving out of order?**

TCP guarantees in-order delivery within a single connection, so out-of-order delivery is impossible on a pure TCP feed. However, it can occur in:
- **UDP multicast feeds** (NSE actually uses this in co-lo) — sequence numbers must be tracked and a reorder buffer maintained.
- **Multi-path connections** — if the feed is aggregated from two network paths.
- **Application-level replay** — when recovering from a gap by replaying from a sequence store.

For out-of-order handling: maintain a min-heap keyed by sequence number. Deliver in order, holding back future messages until the gap is filled (with a timeout after which the gap is declared permanent).

**Q4. How do you prevent buffer overflow with malicious large message lengths?**

Two defences:
1. **Type-based size**: the parser determines expected message size from the `msg_type` field (`msg_total_size()`), not from any length field in the message. Unknown types return 0, causing byte-by-byte scanning rather than trusting a potentially malicious length.
2. **Buffer limit check**: `buf_used + incoming_len > PARSE_BUF_SIZE` triggers a reset. A malicious sender cannot grow the buffer beyond 128 KB.

In a production system, also validate that `symbol_id < MAX_SYMBOLS` before indexing the cache.

---

## Section 4: Lock-Free Symbol Cache

**Q1. How do you prevent readers from seeing inconsistent state during updates?**

The **seqlock** pattern is used. The writer increments a `version` counter to an odd number before writing and to an even number after. Readers:
1. Read `version` (must be even; spin if odd).
2. Copy the data.
3. Read `version` again.
4. If the two versions differ, a write occurred during the read — retry.

This ensures readers always observe a consistent snapshot without using a mutex.

**Q2. What memory ordering do you need for atomic operations?**

| Operation | Ordering | Justification |
|-----------|----------|---------------|
| `version.fetch_add(1)` (begin write) | `memory_order_release` | Writer's subsequent stores must not be reordered before the version increment |
| `atomic_thread_fence` (begin write) | `seq_cst` | Ensure the odd version is visible to all readers before data is written |
| `version.fetch_add(1)` (end write) | `release` | Ensures data stores complete before version increments to even |
| `version.load()` (reader) | `memory_order_acquire` | Synchronises with writer's release; ensures reader sees the writer's data stores |
| Histogram `fetch_add` | `relaxed` | Only the atomicity matters; ordering between buckets is irrelevant for approximate stats |

**Q3. How do you handle cache line bouncing with single writer, visualizer reader?**

Each `MarketState` is `alignas(64)` — one cache line start. With 500 symbols × 64B minimum, the array spans multiple cache lines. The writer updates symbol N; the visualizer reads symbol N+1. Since they are on different cache lines, there is no false sharing. The `alignas(64)` ensures no two symbols share a cache line even if `sizeof(MarketState)` is not a multiple of 64.

The writer and reader may access the *same* symbol's cache line simultaneously (true sharing), but this is handled correctly by the seqlock — the reader will retry if it detects the writer was active.

**Q4. Do you need the Read-Copy-Update (RCU) pattern here?**

No. RCU is designed for the case where: (a) there are many readers, (b) reads must never block or spin, and (c) writes are infrequent. RCU involves maintaining multiple versions of data and garbage-collecting old versions using grace periods.

For this use case: the seqlock is simpler and sufficient because: (a) there is only one writer, (b) writes are frequent (100K/s), (c) readers tolerate brief spins (the write window is ~10ns), and (d) the data fits in a single cache line per symbol (no pointer chasing). RCU would add complexity without benefit here.

---

## Section 5: Terminal Visualization

**Q1. How do you update the display without interfering with network/parsing threads?**

The visualizer runs in a **dedicated thread** that wakes every 500ms. It reads from `SymbolCache` using the lock-free `get_snapshot()` method (no mutex, no blocking). It reads latency stats from `LatencyTracker` using `relaxed` atomic loads (slightly stale values are fine for display). All rendering happens in userspace string manipulation; the only system call is the final `write()` to stdout. This is completely independent of the network thread.

**Q2. Should you use ncurses or raw ANSI codes? Why?**

Raw ANSI codes were chosen for this implementation because:
- **Zero dependencies**: no `-lncurses` linkage required in co-location environments.
- **Simpler**: ncurses has complex initialization (`initscr`, `endwin`, `cbreak`, etc.) and is harder to combine with multi-threaded applications.
- **Sufficient**: for a fixed-layout dashboard that refreshes every 500ms, ncurses' incremental update optimization is not necessary.

ncurses would be preferred if: partial screen updates were needed (avoids full clear), terminal capability detection was required (e.g., supporting dumb terminals), or keyboard input handling was more complex.

**Q3. How do you calculate percentage change when prices update continuously?**

Three approaches:
1. **Open price reference**: store the price at session start (09:15 IST) and compute `(current - open) / open * 100`. Requires a snapshot at open time.
2. **Previous close reference**: store yesterday's closing price (loaded from a file or database at startup).
3. **Rolling window**: compute change vs. the price N seconds ago (e.g., 60-second rolling). This is what the current implementation approximates by comparing bid vs. ask spread — a simplification.

The production approach is (2): load prev_close at startup and display intraday change. The `MarketState` struct would be extended with `open_price` and `prev_close` fields.

---

## Section 7: Performance Measurement

**Q1. Sorting is O(n log n) — how can you calculate percentiles faster?**

Use an **approximate histogram** with exponential-width buckets (powers of 2). Bucket `i` covers `[2^(i-1), 2^i)` nanoseconds. Recording a sample is O(1) (compute bucket index with `63 - __builtin_clzll(ns)`, then atomic increment). Computing a percentile is O(B) where B = 64 buckets — much faster than O(n log n) sort of n samples.

Accuracy: the p99 value is reported as the upper bound of its bucket. At p99=45μs, the bucket covers [32μs, 64μs], giving ~±50% relative error. For monitoring purposes this is acceptable. For precise measurement, use HDR Histogram which uses sub-bucket refinement to achieve 1-tick accuracy at any resolution.

**Q2. How do you minimize the overhead of timestamping?**

Use `CLOCK_MONOTONIC_RAW` with `clock_gettime()` or `std::chrono::steady_clock`. On modern x86, this compiles to a single `rdtsc` instruction (~5ns). Avoid `CLOCK_REALTIME` (subject to NTP adjustments) and `gettimeofday` (lower resolution).

For even lower overhead in co-location: use `rdtsc` directly and convert to nanoseconds using a pre-calibrated TSC frequency. This avoids the vDSO call overhead of `clock_gettime` (~10–20ns) and brings timestamping to ~3ns.

```cpp
inline uint64_t rdtsc_ns() {
    uint64_t tsc;
    __asm__ volatile("rdtsc; shlq $32, %%rdx; orq %%rdx, %0"
                     : "=a"(tsc) : : "rdx");
    return tsc * NS_PER_TICK;  // pre-calibrated constant
}
```

**Q3. What granularity of histogram buckets balances accuracy vs memory?**

Power-of-2 buckets (64 buckets × 8 bytes = 512 bytes) provide:
- Coverage: 1ns to ~9×10^18ns (effectively unlimited)
- Resolution: ±50% relative error (one bucket = one bit of precision)
- Memory: 512 bytes — fits in a single cache line cluster

For better resolution, use **HDR Histogram's** sub-bucket approach: each power-of-2 range is subdivided into 1024 equal-width sub-buckets. This gives 0.1% relative accuracy at the cost of ~1MB memory (2^10 sub-buckets × 64 ranges × 8 bytes). The latency tracker implementation here prioritises simplicity and minimal memory; for production telemetry, HDR Histogram is recommended.
