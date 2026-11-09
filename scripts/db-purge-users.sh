#!/usr/bin/env bash
# Удаление пользователей серверной БД по шаблонам username и/или display_name.
# По умолчанию — только отчёт.
#
# Использование:
#   sudo bash db-purge-users.sh --list
#   sudo bash db-purge-users.sh --like 'anna_%' --like 'maksim_%'          # отчёт
#   sudo bash db-purge-users.sh --display-like 'Rob Bot%' --yes            # удалить
#
set -euo pipefail

DATA_DIR="/var/lib/messengerapp"
USER_PATTERNS=()
NAME_PATTERNS=()
YES=0
LIST=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --data) DATA_DIR="$2"; shift 2 ;;
        --like) USER_PATTERNS+=("$2"); shift 2 ;;
        --display-like) NAME_PATTERNS+=("$2"); shift 2 ;;
        --yes) YES=1; shift ;;
        --list) LIST=1; shift ;;
        *) echo "Неизвестный параметр: $1" >&2; exit 2 ;;
    esac
done

DB="$DATA_DIR/server.db"
if [[ ! -f "$DB" ]]; then
    echo "База не найдена: $DB" >&2
    exit 2
fi
if ! command -v sqlite3 >/dev/null 2>&1; then
    echo "Нужен sqlite3: sudo apt install sqlite3" >&2
    exit 2
fi

if [[ "$LIST" -eq 1 ]]; then
    echo "== Все пользователи =="
    sqlite3 -header -column "$DB" \
        "SELECT u.id, u.username, u.display_name,
                (SELECT COUNT(*) FROM messages m WHERE m.sender_id = u.id) AS messages
         FROM users u ORDER BY u.id;"
    exit 0
fi

if [[ ${#USER_PATTERNS[@]} -eq 0 && ${#NAME_PATTERNS[@]} -eq 0 ]]; then
    echo "Укажите --like 'шаблон%' и/или --display-like 'Имя%' (или --list)" >&2
    exit 2
fi

WHERE=""
for pattern in "${USER_PATTERNS[@]}"; do
    escaped="${pattern//\'/\'\'}"
    if [[ -n "$WHERE" ]]; then WHERE+=" OR "; fi
    WHERE+="username LIKE '$escaped'"
done
for pattern in "${NAME_PATTERNS[@]}"; do
    escaped="${pattern//\'/\'\'}"
    if [[ -n "$WHERE" ]]; then WHERE+=" OR "; fi
    WHERE+="display_name LIKE '$escaped'"
done

echo "== Кандидаты на удаление =="
sqlite3 -header -column "$DB" \
    "SELECT id, username, display_name FROM users WHERE $WHERE ORDER BY id;"

COUNT=$(sqlite3 "$DB" "SELECT COUNT(*) FROM users WHERE $WHERE;")
MESSAGES=$(sqlite3 "$DB" "SELECT COUNT(*) FROM messages WHERE sender_id IN (SELECT id FROM users WHERE $WHERE);")
echo "Пользователей: $COUNT, их сообщений: $MESSAGES"

if [[ "$YES" -ne 1 ]]; then
    echo "== Dry-run: удаление не выполнялось. Для удаления добавьте --yes =="
    exit 0
fi

if [[ "$COUNT" -eq 0 ]]; then
    echo "Нечего удалять."
    exit 0
fi

BACKUP_DIR="$DATA_DIR/backups"
mkdir -p "$BACKUP_DIR"
STAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP="$BACKUP_DIR/server-$STAMP.db"

systemctl stop messengerapp
sqlite3 "$DB" ".backup '$BACKUP'"
echo "Бэкап: $BACKUP"

sqlite3 "$DB" <<SQL
PRAGMA foreign_keys = ON;
BEGIN;
DELETE FROM messages WHERE sender_id IN (SELECT id FROM users WHERE $WHERE);
DELETE FROM users WHERE $WHERE;
DELETE FROM chat_members WHERE user_id NOT IN (SELECT id FROM users) OR chat_id NOT IN (SELECT id FROM chats);
DELETE FROM chats WHERE id NOT IN (SELECT chat_id FROM chat_members);
COMMIT;
VACUUM;
SQL

systemctl start messengerapp

echo "Удалено пользователей: $COUNT. Служба запущена."
echo "Откат: sudo systemctl stop messengerapp && sudo mv $BACKUP $DB && sudo systemctl start messengerapp"
