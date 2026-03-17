#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-9876}"
SYMBOLS="${SYMBOLS:-100}"

echo "Starting Feed Handler  host=$HOST  port=$PORT  symbols=$SYMBOLS"
exec "$BUILD_DIR/feed_handler" \
    --host "$HOST" \
    --port "$PORT" \
    --symbols "$SYMBOLS"
