#!/usr/bin/env bash
# Развёртывание сервера MessengerApp на Ubuntu 22.04+ с TLS и systemd.
# Запуск из корня репозитория: sudo ./scripts/deploy-vps.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="${APP_DIR:-/opt/messengerapp}"
DATA_DIR="${DATA_DIR:-/var/lib/messengerapp}"
SERVICE_USER="${SERVICE_USER:-messengerapp}"
PORT="${PORT:-45000}"

# apt не должен задавать интерактивные вопросы (needrestart, debconf) при
# обновлении библиотек — на свежей ВМ это только мешает автоматизации.
export DEBIAN_FRONTEND=noninteractive
export NEEDRESTART_MODE=a

echo "== Зависимости"
sudo apt-get update
sudo apt-get install -y build-essential cmake qtbase5-dev qtmultimedia5-dev \
    libqt5svg5-dev openssl

echo "== Сборка сервера"
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$ROOT/build" -j"$(nproc)"

echo "== Установка в $APP_DIR"
sudo install -d "$APP_DIR" "$APP_DIR/tls"
sudo install "$ROOT/build/MessengerServer" "$APP_DIR/MessengerServer"

if [ ! -f "$APP_DIR/tls/server.crt" ]; then
    echo "== Сертификат"
    sudo openssl req -x509 -newkey rsa:2048 -sha256 -days 3650 -nodes \
        -keyout "$APP_DIR/tls/server.key" -out "$APP_DIR/tls/server.crt" \
        -subj "/CN=MessengerApp Server" \
        -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
else
    echo "== Сертификат уже существует, оставляем"
fi

echo "== Пользователь и каталог данных"
if ! id -u "$SERVICE_USER" >/dev/null 2>&1; then
    sudo useradd --system --home "$DATA_DIR" --shell /usr/sbin/nologin "$SERVICE_USER"
fi
sudo install -d -o "$SERVICE_USER" -g "$SERVICE_USER" "$DATA_DIR"

# Сертификат создавался от root; служба работает под $SERVICE_USER и должна
# уметь читать ключ, иначе TLS не запускается.
sudo chown -R "$SERVICE_USER:$SERVICE_USER" "$APP_DIR/tls"
sudo chmod 600 "$APP_DIR/tls/server.key"
sudo chmod 644 "$APP_DIR/tls/server.crt"

echo "== systemd"
sudo tee /etc/systemd/system/messengerapp.service >/dev/null <<EOF
[Unit]
Description=MessengerApp Server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=$SERVICE_USER
Group=$SERVICE_USER
ExecStart=$APP_DIR/MessengerServer --port $PORT --data $DATA_DIR \\
    --tls --tls-cert $APP_DIR/tls/server.crt --tls-key $APP_DIR/tls/server.key
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now messengerapp

if command -v ufw >/dev/null 2>&1 && sudo ufw status | grep -q "Status: active"; then
    sudo ufw allow "${PORT}/tcp"
    echo "== ufw: порт ${PORT}/tcp открыт"
fi

echo "== Готово"
sudo systemctl --no-pager --lines=5 status messengerapp || true
sudo openssl x509 -in "$APP_DIR/tls/server.crt" -noout -fingerprint -sha256
