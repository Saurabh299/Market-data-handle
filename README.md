# NSE Market Data Feed Handler

A high-performance, low-latency market data feed handler for NSE co-location environments.  
Implements a complete exchange simulator (server) and feed handler (client) over TCP.

---

## Architecture Overview

```
Exchange Simulator ──── Binary TCP ────▶ Feed Handler
  (GBM tick gen)         (44 bytes)       (epoll ET)
  (epoll server)                          (zero-copy parser)
  (100 symbols)                           (seqlock cache)
                                          (ANSI dashboard)
```

See [docs/DESIGN.md](docs/DESIGN.md) for full architecture documentation.

---

## Prerequisites

- Linux (epoll required)
- GCC 9+ or Clang 10+ with C++17
- CMake 3.16+
- pthreads
- Google Test (optional, for unit tests): `sudo apt install libgtest-dev`

---

## Building

```bash
# Release build (optimised)
bash scripts/build.sh Release

# Debug build (with ASan + UBSan)
bash scripts/build.sh Debug
```

Binaries are placed in `build/`.

---

## Running

### Full demo (server + client)
```bash
bash scripts/run_demo.sh
```

### Server only
```bash
PORT=9876 SYMBOLS=100 RATE=100000 FAULT=1 bash scripts/run_server.sh
```

| Flag | Default | Description |
|------|---------|-------------|
| `--port N` | 9876 | TCP listen port |
| `--symbols N` | 100 | Number of simulated symbols |
| `--rate N` | 100000 | Target tick rate (msg/s) |
| `--fault` | off | Enable sequence gap injection |

### Client only
```bash
HOST=127.0.0.1 PORT=9876 SYMBOLS=100 bash scripts/run_client.sh
```

| Flag | Default | Description |
|------|---------|-------------|
| `--host H` | 127.0.0.1 | Server hostname/IP |
| `--port N` | 9876 | Server port |
| `--symbols N` | 100 | Expected symbol count |
| `--no-viz` | off | Disable terminal dashboard |

---

## Running Tests

```bash
cd build && ctest --output-on-failure
# or directly:
./run_tests
```

Tests cover: protocol layout, checksum, parser fragmentation, sequence gaps, cache consistency, latency tracker accuracy.

---

## Running Benchmarks

```bash
bash scripts/benchmark_latency.sh
```

Outputs:
- Parser throughput (full-message and fragmented)
- Symbol cache update and read latency (p50/p99/p999)
- CSV histograms: `bench_read_lat.csv`, `bench_write_lat.csv`

---

## Project Structure

```
├── src/
│   ├── server/
│   │   ├── exchange_simulator.{h,cpp}   # TCP server, epoll, broadcast
│   │   ├── tick_generator.{h,cpp}       # GBM, Box-Muller
│   │   └── main_server.cpp
│   └── client/
│       ├── feed_handler.cpp             # Main client loop
│       ├── socket.{h,cpp}               # Non-blocking TCP, retry
│       ├── parser.{h,cpp}               # Zero-copy binary parser
│       └── visualizer.{h,cpp}           # ANSI terminal dashboard
├── include/
│   ├── protocol.h                       # Shared wire format
│   ├── cache.h                          # Lock-free seqlock cache
│   ├── latency_tracker.h                # Atomic histogram
│   └── memory_pool.h                    # Lock-free buffer pool
├── tests/
│   ├── test_parser.cpp
│   ├── test_cache.cpp
│   ├── test_latency_tracker.cpp
│   └── test_protocol.cpp
├── benchmarks/
│   ├── bench_parser.cpp
│   └── bench_cache.cpp
├── docs/
│   ├── DESIGN.md                        # Architecture & decisions
│   ├── GBM.md                           # Mathematical background
│   ├── NETWORK.md                       # Socket implementation details
│   ├── PERFORMANCE.md                   # Benchmark results
│   └── QUESTIONS.md                     # All critical thinking answers
├── scripts/
│   ├── build.sh
│   ├── run_demo.sh
│   ├── run_server.sh
│   ├── run_client.sh
│   └── benchmark_latency.sh
└── CMakeLists.txt
```

---

## Performance Summary

| Metric | Value |
|--------|-------|
| Parser throughput | ~12M msg/s (full messages) |
| End-to-end latency p50 | ~15 μs (loopback) |
| End-to-end latency p99 | ~45 μs |
| End-to-end latency p999 | ~120 μs |
| Cache read latency p50 | ~12 ns |
| Cache write latency p50 | ~18 ns |
| Tick generation rate | 10K – 500K msg/s configurable |

---

## Protocol

All messages are little-endian packed structs:

```
Header (16 bytes): msg_type(2) | seq_no(4) | timestamp_ns(8) | symbol_id(2)
Trade  payload  (12 bytes): price(8) | quantity(4)
Quote  payload  (24 bytes): bid_price(8) | bid_qty(4) | ask_price(8) | ask_qty(4)
Heartbeat payload: (none)
Trailer (4 bytes): XOR checksum of all preceding bytes
```

Full specification in [include/protocol.h](include/protocol.h).

---

## Documentation

| Document | Contents |
|----------|----------|
| [DESIGN.md](docs/DESIGN.md) | Full architecture, threading, memory, concurrency |
| [GBM.md](docs/GBM.md) | GBM mathematics, Box-Muller, parameter rationale |
| [NETWORK.md](docs/NETWORK.md) | epoll design, TCP stream handling, reconnection |
| [PERFORMANCE.md](docs/PERFORMANCE.md) | Benchmark methodology, results, optimisation comparisons |
| [QUESTIONS.md](docs/QUESTIONS.md) | All critical thinking question answers |
