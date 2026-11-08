#!/usr/bin/env bash
# Установка обновления MessengerApp из tar.gz с манифестом.
# Использование:
#   sudo bash deploy-update.sh /tmp/messenger-updates-2.1.0.tar.gz
#   sudo bash deploy-update.sh archive.tar.gz --dry-run
#   sudo bash deploy-update.sh archive.tar.gz --dir /opt/messengerapp/updates

set -euo pipefail

ARCHIVE="${1:-}"
shift || true

UPDATES_DIR="/var/lib/messengerapp/updates"
OWNER="messengerapp:messengerapp"
DRY_RUN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dir) UPDATES_DIR="$2"; shift 2 ;;
        --owner) OWNER="$2"; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        *)
            echo "Неизвестный параметр: $1" >&2
            exit 2
            ;;
    esac
done

if [[ -z "$ARCHIVE" || ! -f "$ARCHIVE" ]]; then
    echo "Архив не найден: ${ARCHIVE:-<пусто>}" >&2
    exit 2
fi

if [[ "$UPDATES_DIR" != */updates* ]]; then
    echo "Подозрительный путь обновлений: $UPDATES_DIR" >&2
    exit 2
fi

WORK="$(mktemp -d)"
cleanup() {
    rm -rf "$WORK"
}
trap cleanup EXIT

echo "== Распаковка =="
tar -xzf "$ARCHIVE" -C "$WORK"

MANIFEST="$(find "$WORK" -maxdepth 2 -name manifest.json -print -quit)"
if [[ -z "$MANIFEST" ]]; then
    echo "В архиве нет manifest.json" >&2
    exit 3
fi
SOURCE_DIR="$(dirname "$MANIFEST")"
echo "Каталог: $SOURCE_DIR"

VERSION="$(python3 -c "import json;print(json.load(open('$MANIFEST'))['version'])")"
COUNT="$(python3 -c "import json;print(len(json.load(open('$MANIFEST'))['files']))")"
echo "Версия: $VERSION, файлов: $COUNT"

echo "== Проверка хешей =="
python3 - "$SOURCE_DIR" <<'PY'
import hashlib, json, os, sys
root = sys.argv[1]
m = json.load(open(os.path.join(root, 'manifest.json')))
bad = 0
for f in m['files']:
    p = os.path.join(root, f['path'])
    if not os.path.exists(p):
        print('MISSING', f['path']); bad += 1; continue
    if os.path.getsize(p) != f['size']:
        print('SIZE', f['path']); bad += 1; continue
    if hashlib.sha256(open(p, 'rb').read()).hexdigest() != f['sha256']:
        print('HASH', f['path']); bad += 1
print('checked', len(m['files']), 'bad', bad)
sys.exit(1 if bad else 0)
PY

if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "== Dry-run: каталог не изменён =="
    exit 0
fi

NEW_DIR="${UPDATES_DIR}.new"
OLD_DIR="${UPDATES_DIR}.old"
SWAPPED=0

rollback() {
    if [[ "$SWAPPED" -eq 1 && -d "$OLD_DIR" ]]; then
        echo "!! Откат к предыдущей версии" >&2
        rm -rf "$UPDATES_DIR"
        mv "$OLD_DIR" "$UPDATES_DIR"
    fi
}
trap 'rollback; cleanup' EXIT

echo "== Подготовка $NEW_DIR =="
rm -rf "$NEW_DIR"
mkdir -p "$NEW_DIR"
cp -a "$SOURCE_DIR/." "$NEW_DIR/"
chown -R "$OWNER" "$NEW_DIR"
chmod -R u+rwX,go+rX "$NEW_DIR"

echo "== Подмена каталога =="
rm -rf "$OLD_DIR"
if [[ -d "$UPDATES_DIR" ]]; then
    mv "$UPDATES_DIR" "$OLD_DIR"
fi
SWAPPED=1
mv "$NEW_DIR" "$UPDATES_DIR"
SWAPPED=0

echo "== Готово =="
echo "Версия $VERSION установлена в $UPDATES_DIR"
echo "Проверка: sudo head -c 80 $UPDATES_DIR/manifest.json; echo"
