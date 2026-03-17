#!/usr/bin/env bash
# run_demo.sh – starts server + client for a complete live demo
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."
BUILD_DIR="$ROOT/build"

PORT=9876
SYMBOLS=100
RATE=100000

# Build 
if [[ ! -f "$BUILD_DIR/exchange_server" ]]; then
    echo "Binaries not found – building first..."
    bash "$SCRIPT_DIR/build.sh"
fi

echo "=== NSE Market Data Feed Handler Demo ==="
echo "  Port   : $PORT"
echo "  Symbols: $SYMBOLS"
echo "  Rate   : $RATE msg/s"
echo ""

# Launch server in background
echo "Starting exchange simulator..."
"$BUILD_DIR/exchange_server" \
    --port "$PORT" \
    --symbols "$SYMBOLS" \
    --rate "$RATE" \
    --fault &
SERVER_PID=$!
echo "  Server PID: $SERVER_PID"

# Give server time to bind
sleep 1

# Launch client (foreground – shows live dashboard)
echo "Starting feed handler (Ctrl+C to stop)..."
"$BUILD_DIR/feed_handler" \
    --host 127.0.0.1 \
    --port "$PORT" \
    --symbols "$SYMBOLS"

# Cleanup
kill "$SERVER_PID" 2>/dev/null || true
echo "Demo finished."
