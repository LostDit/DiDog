#!/usr/bin/env bash
# Демо: локальный сервер + два клиента с разными профилями.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
PORT="${1:-45000}"
DATA="$BUILD_DIR/demo-data"

mkdir -p "$DATA"

"$BUILD_DIR/MessengerServer" --port "$PORT" --data "$DATA" &
SERVER_PID=$!

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    pkill -P $$ 2>/dev/null || true
}
trap cleanup EXIT

sleep 1
"$BUILD_DIR/MessengerClient" --data "$DATA" --profile alice &
"$BUILD_DIR/MessengerClient" --data "$DATA" --profile bob &
wait
