# Обслуживание и очистка БД

Скрипты запускаются на сервере, все изменения — только через бэкап. По умолчанию
скрипты работают в режиме отчёта (dry-run); для изменений нужен явный флаг.

## 1. Обслуживание без удаления данных

```bash
sudo bash db-maintenance.sh --dry-run   # отчёт: целостность, счётчики, сироты
sudo bash db-maintenance.sh             # бэкап + чистка сирот + VACUUM
```

Что делает:

- `PRAGMA integrity_check`;
- бэкап в `/var/lib/messengerapp/backups/server-<время>.db` (без остановки службы);
- удаляет строки-сироты: квитанции и вложения без сообщения, участников без
  чата/пользователя, сообщения без чата;
- удаляет файлы вложений, на которые нет ссылок;
- `VACUUM` и `ANALYZE`.

Нужен `sqlite3`: `sudo apt install sqlite3`.

## 2. Полный сброс серверной БД

```bash
sudo bash db-reset.sh        # покажет план
sudo bash db-reset.sh --yes  # выполнить
```

Останавливает службу, переносит `server.db` и `attachments/` в `backups/`,
запускает службу — она создаст пустую схему (v6) с общим чатом. Все аккаунты
и сообщения пропадают; откат — команды из вывода скрипта.

## 3. Удаление тестовых пользователей

```bash
sudo bash db-purge-users.sh --like 'anna_%' --like 'maksim_%'          # отчёт
sudo bash db-purge-users.sh --like 'anna_%' --like 'maksim_%' --yes    # удалить
```

Удаляет пользователей по `username LIKE`, их сообщения (вложения и квитанции
каскадом) и осиротевшие чаты. Перед удалением делает бэкап и останавливает
службу.

## 4. Откат

Все скрипты оставляют бэкап с временной меткой:

```bash
sudo systemctl stop messengerapp
sudo mv /var/lib/messengerapp/backups/server-<время>.db /var/lib/messengerapp/server.db
sudo systemctl start messengerapp
```

## 5. Локальная БД клиента

Кэш вложений и локальная история лежат в `%APPDATA%\MessengerApp` (Windows).
Очистка из клиента: Настройки → Данные → «Открыть папку». Удаление кэша
`attachments` безопасно — файлы докачаются заново.
