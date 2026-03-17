#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"

if [[ ! -f "$BUILD_DIR/bench_parser" ]]; then
    bash "$SCRIPT_DIR/build.sh"
fi

echo "=== Parser Throughput Benchmark ==="
"$BUILD_DIR/bench_parser"

echo ""
echo "=== Symbol Cache Latency Benchmark ==="
"$BUILD_DIR/bench_cache"

echo ""
echo "=== Histogram CSVs written to current directory ==="
ls -lh bench_*.csv latency_histogram.csv 2>/dev/null || true
