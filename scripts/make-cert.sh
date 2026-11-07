#!/usr/bin/env bash
# Генерирует самоподписанный сертификат для TLS-сервера.
# Использование: ./scripts/make-cert.sh [каталог] ["Common Name"]
set -euo pipefail

OUT_DIR="${1:-tls}"
COMMON_NAME="${2:-MessengerApp Server}"

mkdir -p "$OUT_DIR"

if [ -f "$OUT_DIR/server.crt" ] && [ -f "$OUT_DIR/server.key" ]; then
    echo "Сертификат уже существует: $OUT_DIR/server.crt"
    exit 0
fi

openssl req -x509 -newkey rsa:2048 -sha256 -days 3650 -nodes \
    -keyout "$OUT_DIR/server.key" -out "$OUT_DIR/server.crt" \
    -subj "/CN=$COMMON_NAME" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"

echo "Сертификат: $OUT_DIR/server.crt"
openssl x509 -in "$OUT_DIR/server.crt" -noout -fingerprint -sha256
