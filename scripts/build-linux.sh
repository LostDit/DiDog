#!/usr/bin/env bash
# Сборка MessengerApp под Linux (Qt 5.14+).
#   sudo apt install build-essential cmake qtbase5-dev qtmultimedia5-dev libqt5svg5-dev
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${1:-Debug}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "Готово: $BUILD_DIR"
