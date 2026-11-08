#include "data/Migrations.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

namespace data::migrations {
namespace {

// Схема v1. Одна и та же для сервера и клиента: клиент просто не заполняет pwd_*,
// поэтому миграции не расходятся и код репозиториев общий.
const char *const kSchemaV1[] = {
    "CREATE TABLE IF NOT EXISTS meta ("
    "  key   TEXT PRIMARY KEY,"
    "  value TEXT NOT NULL"
    ")",

    "CREATE TABLE IF NOT EXISTS users ("
    "  id           INTEGER PRIMARY KEY,"
    "  username     TEXT    NOT NULL COLLATE NOCASE UNIQUE,"
    "  display_name TEXT    NOT NULL,"
    "  pwd_salt     BLOB,"
    "  pwd_hash     BLOB,"
    "  pwd_iters    INTEGER NOT NULL DEFAULT 0,"
    "  avatar_hue   INTEGER NOT NULL DEFAULT 210,"
    "  created_at   INTEGER NOT NULL"
    ")",

    "CREATE TABLE IF NOT EXISTS sessions ("
    "  token      TEXT PRIMARY KEY,"
    "  user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
    "  created_at INTEGER NOT NULL,"
    "  last_seen  INTEGER NOT NULL,"
    "  expires_at INTEGER NOT NULL"
    ")",

    "CREATE TABLE IF NOT EXISTS chats ("
    "  id         INTEGER PRIMARY KEY,"
    "  kind       TEXT NOT NULL CHECK (kind IN ('direct','group')),"
    "  title      TEXT,"
    "  created_at INTEGER NOT NULL"
    ")",

    "CREATE TABLE IF NOT EXISTS chat_members ("
    "  chat_id       INTEGER NOT NULL REFERENCES chats(id) ON DELETE CASCADE,"
    "  user_id       INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
    "  last_read_seq INTEGER NOT NULL DEFAULT 0,"
    "  joined_at     INTEGER NOT NULL,"
    "  PRIMARY KEY (chat_id, user_id)"
    ")",

    "CREATE TABLE IF NOT EXISTS messages ("
    "  id            INTEGER PRIMARY KEY,"
    "  chat_id       INTEGER NOT NULL REFERENCES chats(id) ON DELETE CASCADE,"
    "  sender_id     INTEGER NOT NULL REFERENCES users(id),"
    "  seq           INTEGER NOT NULL,"
    "  client_msg_id TEXT    NOT NULL,"
    "  kind          TEXT    NOT NULL CHECK (kind IN ('text','image','video','gif','file')),"
    "  body          TEXT    NOT NULL DEFAULT '',"
    "  created_at    INTEGER NOT NULL,"
    "  UNIQUE (chat_id, seq)"
    ")",

    "CREATE UNIQUE INDEX IF NOT EXISTS ux_msg_client ON messages(sender_id, client_msg_id)",
    "CREATE INDEX IF NOT EXISTS ix_msg_chat ON messages(chat_id, seq DESC)",

    "CREATE TABLE IF NOT EXISTS attachments ("
    "  id          INTEGER PRIMARY KEY,"
    "  message_id  INTEGER NOT NULL UNIQUE REFERENCES messages(id) ON DELETE CASCADE,"
    "  file_name   TEXT    NOT NULL,"
    "  mime        TEXT    NOT NULL,"
    "  byte_size   INTEGER NOT NULL,"
    "  sha256      TEXT    NOT NULL,"
    "  rel_path    TEXT    NOT NULL,"
    "  thumb_path  TEXT,"
    "  width       INTEGER,"
    "  height      INTEGER,"
    "  duration_ms INTEGER"
    ")",

    "CREATE TABLE IF NOT EXISTS receipts ("
    "  message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE,"
    "  user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
    "  state      TEXT    NOT NULL CHECK (state IN ('delivered','read')),"
    "  ts         INTEGER NOT NULL,"
    "  PRIMARY KEY (message_id, user_id, state)"
    ")",

    // Офлайн-очередь клиента. На сервере просто не используется.
    "CREATE TABLE IF NOT EXISTS outbox ("
    "  client_msg_id TEXT PRIMARY KEY,"
    "  chat_id       INTEGER NOT NULL,"
    "  kind          TEXT    NOT NULL,"
    "  body          TEXT,"
    "  attach_path   TEXT,"
    "  state         TEXT    NOT NULL DEFAULT 'pending',"
    "  attempts      INTEGER NOT NULL DEFAULT 0,"
    "  created_at    INTEGER NOT NULL"
    ")",

    "CREATE INDEX IF NOT EXISTS ix_outbox_chat ON outbox(chat_id, created_at)",
};

// Схема v2: кэш списка чатов для мгновенного показа истории при запуске.
const char *const kSchemaV2[] = {
    "CREATE TABLE IF NOT EXISTS chat_cache ("
    "  chat_id   INTEGER PRIMARY KEY,"
    "  kind      TEXT    NOT NULL,"
    "  title     TEXT,"
    "  peer_id   INTEGER NOT NULL DEFAULT 0,"
    "  peer_name TEXT    NOT NULL DEFAULT '',"
    "  peer_hue  INTEGER NOT NULL DEFAULT 210,"
    "  last_body TEXT    NOT NULL DEFAULT '',"
    "  last_kind TEXT    NOT NULL DEFAULT 'text',"
    "  last_at   INTEGER NOT NULL DEFAULT 0,"
    "  last_seq  INTEGER NOT NULL DEFAULT 0,"
    "  unread    INTEGER NOT NULL DEFAULT 0"
    ")",
};

// Схема v3: удаление и редактирование сообщений.
const char *const kSchemaV3[] = {
    "ALTER TABLE messages ADD COLUMN deleted INTEGER NOT NULL DEFAULT 0",
    "ALTER TABLE messages ADD COLUMN edited_at INTEGER NOT NULL DEFAULT 0",
};

bool exec(QSqlDatabase &db, const QString &sql, QString *error)
{
    QSqlQuery query(db);
    if (query.exec(sql))
        return true;

    if (error) {
        *error = QStringLiteral("Ошибка миграции: %1\nSQL: %2")
                     .arg(query.lastError().text(), sql);
    }
    return false;
}

bool applyV1(QSqlDatabase &db, QString *error)
{
    if (!db.transaction()) {
        if (error)
            *error = QStringLiteral("Не удалось начать транзакцию: %1")
                         .arg(db.lastError().text());
        return false;
    }

    for (const char *statement : kSchemaV1) {
        if (!exec(db, QString::fromLatin1(statement), error)) {
            db.rollback();
            return false;
        }
    }

    if (!exec(db, QStringLiteral("INSERT OR REPLACE INTO meta(key, value) "
                                 "VALUES ('schema_version', '%1')").arg(kCurrentVersion),
              error)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        if (error)
            *error = QStringLiteral("Не удалось зафиксировать миграцию: %1")
                         .arg(db.lastError().text());
        return false;
    }
    return true;
}

bool applyV2(QSqlDatabase &db, QString *error)
{
    if (!db.transaction()) {
        if (error)
            *error = QStringLiteral("Не удалось начать транзакцию: %1")
                         .arg(db.lastError().text());
        return false;
    }

    for (const char *statement : kSchemaV2) {
        if (!exec(db, QString::fromLatin1(statement), error)) {
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        if (error)
            *error = QStringLiteral("Не удалось зафиксировать миграцию: %1")
                         .arg(db.lastError().text());
        return false;
    }
    return true;
}

} // namespace

bool applyV3(QSqlDatabase &db, QString *error)
{
    if (!db.transaction()) {
        if (error)
            *error = QStringLiteral("Не удалось начать транзакцию: %1")
                         .arg(db.lastError().text());
        return false;
    }

    for (const char *statement : kSchemaV3) {
        if (!exec(db, QString::fromLatin1(statement), error)) {
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        if (error)
            *error = QStringLiteral("Не удалось зафиксировать миграцию: %1")
                         .arg(db.lastError().text());
        return false;
    }
    return true;
}



int version(QSqlDatabase &db)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next())
        return -1;
    return query.value(0).toInt();
}

bool apply(QSqlDatabase &db, QString *error)
{
    const int current = version(db);
    if (current < 0) {
        if (error)
            *error = QStringLiteral("Не удалось прочитать версию схемы БД.");
        return false;
    }

    if (current > kCurrentVersion) {
        if (error) {
            *error = QStringLiteral("База создана более новой версией приложения "
                                    "(схема %1, поддерживается %2). Обновите приложение.")
                         .arg(current)
                         .arg(kCurrentVersion);
        }
        return false;
    }

    if (current == kCurrentVersion)
        return true;

    if (current < 1 && !applyV1(db, error))
        return false;

    if (current < 2 && !applyV2(db, error))
        return false;

    if (current < 3 && !applyV3(db, error))
        return false;

    if (!exec(db, QStringLiteral("PRAGMA user_version = %1").arg(kCurrentVersion), error))
        return false;

    return true;
}

} // namespace data::migrations
