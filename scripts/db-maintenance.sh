#!/usr/bin/env bash
# Обслуживание БД MessengerApp: бэкап, integrity_check, удаление сирот, VACUUM.
# Безопасно при работающей службе (бэкап через .backup, WAL).
#
# Использование:
#   sudo bash db-maintenance.sh --dry-run      # только отчёт
#   sudo bash db-maintenance.sh                # бэкап + чистка + VACUUM
#
set -euo pipefail

DATA_DIR="/var/lib/messengerapp"
DRY_RUN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --data) DATA_DIR="$2"; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        *) echo "Неизвестный параметр: $1" >&2; exit 2 ;;
    esac
done

DB="$DATA_DIR/server.db"
BACKUP_DIR="$DATA_DIR/backups"

if [[ ! -f "$DB" ]]; then
    echo "База не найдена: $DB" >&2
    exit 2
fi

if ! command -v sqlite3 >/dev/null 2>&1; then
    echo "Нужен sqlite3: sudo apt install sqlite3" >&2
    exit 2
fi

echo "== База: $DB =="
sqlite3 "$DB" 'PRAGMA integrity_check;' | head -n 1

count() {
    sqlite3 "$DB" "SELECT COUNT(*) FROM $1;"
}

users=$(count users)
chats=$(count chats)
messages=$(count messages)
attachments=$(count attachments)
receipts=$(count receipts)
echo "До: пользователей $users, чатов $chats, сообщений $messages, вложений $attachments, квитанций $receipts"

orphan_receipts=$(sqlite3 "$DB" "SELECT COUNT(*) FROM receipts WHERE message_id NOT IN (SELECT id FROM messages);")
orphan_attachments=$(sqlite3 "$DB" "SELECT COUNT(*) FROM attachments WHERE message_id NOT IN (SELECT id FROM messages);")
orphan_members=$(sqlite3 "$DB" "SELECT COUNT(*) FROM chat_members WHERE chat_id NOT IN (SELECT id FROM chats) OR user_id NOT IN (SELECT id FROM users);")
orphan_messages=$(sqlite3 "$DB" "SELECT COUNT(*) FROM messages WHERE chat_id NOT IN (SELECT id FROM chats);")
orphan_files=0
if [[ -d "$DATA_DIR/attachments" ]]; then
    while IFS= read -r file; do
        rel="${file#"$DATA_DIR/attachments/"}"
        hit=$(sqlite3 "$DB" "SELECT COUNT(*) FROM attachments WHERE rel_path = '$rel' OR thumb_path = '$rel';")
        if [[ "$hit" == "0" ]]; then
            orphan_files=$((orphan_files + 1))
        fi
    done < <(find "$DATA_DIR/attachments" -type f 2>/dev/null)
fi

echo "Сироты: квитанций $orphan_receipts, вложений-строк $orphan_attachments, участников $orphan_members, сообщений $orphan_messages, файлов $orphan_files"

if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "== Dry-run: изменения не вносились =="
    exit 0
fi

echo "== Бэкап =="
mkdir -p "$BACKUP_DIR"
STAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP="$BACKUP_DIR/server-$STAMP.db"
sqlite3 "$DB" ".backup '$BACKUP'"
echo "Бэкап: $BACKUP ($(du -h "$BACKUP" | cut -f1))"

echo "== Чистка =="
sqlite3 "$DB" <<'SQL'
PRAGMA foreign_keys = ON;
BEGIN;
DELETE FROM receipts WHERE message_id NOT IN (SELECT id FROM messages);
DELETE FROM attachments WHERE message_id NOT IN (SELECT id FROM messages);
DELETE FROM chat_members WHERE chat_id NOT IN (SELECT id FROM chats) OR user_id NOT IN (SELECT id FROM users);
DELETE FROM messages WHERE chat_id NOT IN (SELECT id FROM chats);
COMMIT;
SQL

if [[ "$orphan_files" -gt 0 && -d "$DATA_DIR/attachments" ]]; then
    echo "Удаление файлов-сирот…"
    while IFS= read -r file; do
        rel="${file#"$DATA_DIR/attachments/"}"
        hit=$(sqlite3 "$DB" "SELECT COUNT(*) FROM attachments WHERE rel_path = '$rel' OR thumb_path = '$rel';")
        if [[ "$hit" == "0" ]]; then
            rm -f "$file"
        fi
    done < <(find "$DATA_DIR/attachments" -type f 2>/dev/null)
fi

echo "== VACUUM =="
sqlite3 "$DB" 'VACUUM; ANALYZE;'

echo "После: пользователей $(count users), чатов $(count chats), сообщений $(count messages), вложений $(count attachments)"
echo "Размер БД: $(du -h "$DB" | cut -f1)"
echo "Готово."
