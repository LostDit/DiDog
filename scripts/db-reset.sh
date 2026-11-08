#!/usr/bin/env bash
# Полный сброс серверной БД MessengerApp с бэкапом.
# Все пользователи, чаты и сообщения будут удалены; служба пересоздаст пустую БД.
#
# Использование:
#   sudo bash db-reset.sh            # покажет, что будет сделано
#   sudo bash db-reset.sh --yes      # выполнить
#
set -euo pipefail

DATA_DIR="/var/lib/messengerapp"
YES=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --data) DATA_DIR="$2"; shift 2 ;;
        --yes) YES=1; shift ;;
        *) echo "Неизвестный параметр: $1" >&2; exit 2 ;;
    esac
done

DB="$DATA_DIR/server.db"

if [[ "$YES" -ne 1 ]]; then
    echo "Будет выполнено:"
    echo "  1) systemctl stop messengerapp"
    echo "  2) бэкап $DB -> $DATA_DIR/backups/server-<время>.db"
    echo "  3) перенос attachments -> $DATA_DIR/backups/attachments-<время>"
    echo "  4) systemctl start messengerapp (создаст пустую БД)"
    echo ""
    echo "ВСЕ аккаунты и сообщения будут удалены. Для выполнения:"
    echo "  sudo bash $0 --yes"
    exit 0
fi

if [[ ! -f "$DB" ]]; then
    echo "База не найдена: $DB" >&2
    exit 2
fi

BACKUP_DIR="$DATA_DIR/backups"
mkdir -p "$BACKUP_DIR"
STAMP="$(date +%Y%m%d-%H%M%S)"

systemctl stop messengerapp

mv "$DB" "$BACKUP_DIR/server-$STAMP.db"
if [[ -d "$DATA_DIR/attachments" ]]; then
    mv "$DATA_DIR/attachments" "$BACKUP_DIR/attachments-$STAMP"
fi

systemctl start messengerapp

echo "Бэкап: $BACKUP_DIR/server-$STAMP.db"
echo "База сброшена, служба запущена. Проверка:"
echo "  sudo systemctl status messengerapp --no-pager | head"
echo "Откат:"
echo "  sudo systemctl stop messengerapp"
echo "  sudo mv $BACKUP_DIR/server-$STAMP.db $DB"
echo "  sudo mv $BACKUP_DIR/attachments-$STAMP $DATA_DIR/attachments"
echo "  sudo systemctl start messengerapp"
