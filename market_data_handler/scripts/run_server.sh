#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"

PORT="${PORT:-9876}"
SYMBOLS="${SYMBOLS:-100}"
RATE="${RATE:-100000}"
FAULT="${FAULT:-}"

FAULT_FLAG=""
[[ -n "$FAULT" ]] && FAULT_FLAG="--fault"

echo "Starting Exchange Simulator on port $PORT  symbols=$SYMBOLS  rate=$RATE/s"
exec "$BUILD_DIR/exchange_server" \
    --port "$PORT" \
    --symbols "$SYMBOLS" \
    --rate "$RATE" \
    $FAULT_FLAG
