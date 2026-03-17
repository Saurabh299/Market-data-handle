cmake_minimum_required(VERSION 3.16)
project(MarketDataHandler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include_directories(include src/server src/client src/common)

# ─── Per-config compile options (avoids string-parsing issues) ────────────────
add_compile_options(
    -Wall
    -Wextra
    -Wshadow
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-march=native>
    $<$<CONFIG:Release>:-funroll-loops>
    $<$<CONFIG:Release>:-fno-omit-frame-pointer>
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

add_link_options(
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

# ─── Common sources ───────────────────────────────────────────────────────────
set(COMMON_SRCS
    src/common/cache.cpp
    src/common/memory_pool.cpp
    src/common/latency_tracker.cpp
)

# ─── Server ───────────────────────────────────────────────────────────────────
add_executable(exchange_server
    src/server/main_server.cpp
    src/server/exchange_simulator.cpp
    src/server/tick_generator.cpp
    src/server/client_manager.cpp
    ${COMMON_SRCS}
)
target_include_directories(exchange_server PRIVATE src/server src/common include)
target_link_libraries(exchange_server pthread)

# ─── Client / Feed Handler ────────────────────────────────────────────────────
add_executable(feed_handler
    src/client/feed_handler.cpp
    src/client/socket.cpp
    src/client/parser.cpp
    src/client/visualizer.cpp
    ${COMMON_SRCS}
)
target_include_directories(feed_handler PRIVATE src/client src/common include)
target_link_libraries(feed_handler pthread)

# ─── Tests (Google Test) ──────────────────────────────────────────────────────
find_package(GTest QUIET)
if(GTest_FOUND)
    enable_testing()
    add_executable(run_tests
        tests/test_parser.cpp
        tests/test_cache.cpp
        tests/test_latency_tracker.cpp
        tests/test_protocol.cpp
        src/client/parser.cpp
        ${COMMON_SRCS}
    )
    target_include_directories(run_tests PRIVATE src/client src/common include)
    target_link_libraries(run_tests GTest::GTest GTest::Main pthread)
    add_test(NAME UnitTests COMMAND run_tests)
endif()

# ─── Benchmarks ───────────────────────────────────────────────────────────────
add_executable(bench_parser
    benchmarks/bench_parser.cpp
    src/client/parser.cpp
)
target_include_directories(bench_parser PRIVATE src/client include)
target_link_libraries(bench_parser pthread)
target_compile_options(bench_parser PRIVATE -O3 -march=native)

add_executable(bench_cache
    benchmarks/bench_cache.cpp
    ${COMMON_SRCS}
)
target_include_directories(bench_cache PRIVATE src/common include)
target_link_libraries(bench_cache pthread)
target_compile_options(bench_cache PRIVATE -O3 -march=native)
