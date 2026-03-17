#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."
BUILD_DIR="$ROOT/build"

BUILD_TYPE="${1:-Release}"

echo "=== Building Market Data Feed Handler (${BUILD_TYPE}) ==="

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
cmake --build . --parallel "$NPROC"

echo ""
echo "=== Build complete ==="
echo "  Binaries:"
echo "    $BUILD_DIR/exchange_server"
echo "    $BUILD_DIR/feed_handler"
echo "    $BUILD_DIR/bench_parser"
echo "    $BUILD_DIR/bench_cache"
[[ -f "$BUILD_DIR/run_tests" ]] && echo "    $BUILD_DIR/run_tests"
