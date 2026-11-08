#include "data/MessageRepository.h"

#include "data/SqlUtil.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

namespace data {
namespace {

const char kSelectMessage[] =
    "SELECT m.id, m.chat_id, m.sender_id, m.seq, m.client_msg_id, m.kind, m.body, m.created_at, "
    "       m.deleted AS deleted, m.edited_at AS edited_at, "
    "       u.display_name AS sender_name, u.avatar_hue AS sender_hue, "
    "       a.id AS att_id, a.file_name AS att_name, a.mime AS att_mime, "
    "       a.byte_size AS att_size, a.sha256 AS att_sha, a.rel_path AS att_path, "
    "       a.thumb_path AS att_thumb, a.width AS att_width, a.height AS att_height, "
    "       a.duration_ms AS att_duration "
    "FROM messages m "
    "JOIN users u ON u.id = m.sender_id "
    "LEFT JOIN attachments a ON a.message_id = m.id ";

Message readMessage(const QSqlQuery &query)
{
    Message message;
    message.id = query.value(QStringLiteral("id")).toLongLong();
    message.chatId = query.value(QStringLiteral("chat_id")).toLongLong();
    message.senderId = query.value(QStringLiteral("sender_id")).toLongLong();
    message.seq = query.value(QStringLiteral("seq")).toLongLong();
    message.clientMsgId = query.value(QStringLiteral("client_msg_id")).toString();
    message.kind = messageKindFromString(query.value(QStringLiteral("kind")).toString());
    message.body = query.value(QStringLiteral("body")).toString();
    message.createdAt = query.value(QStringLiteral("created_at")).toLongLong();
    message.deleted = query.value(QStringLiteral("deleted")).toBool();
    message.editedAt = query.value(QStringLiteral("edited_at")).toLongLong();
    message.senderDisplayName = query.value(QStringLiteral("sender_name")).toString();
    message.senderHue = query.value(QStringLiteral("sender_hue")).toInt();

    const QVariant attachmentId = query.value(QStringLiteral("att_id"));
    if (!attachmentId.isNull()) {
        Attachment attachment;
        attachment.id = attachmentId.toLongLong();
        attachment.messageId = message.id;
        attachment.fileName = query.value(QStringLiteral("att_name")).toString();
        attachment.mime = query.value(QStringLiteral("att_mime")).toString();
        attachment.byteSize = query.value(QStringLiteral("att_size")).toLongLong();
        attachment.sha256 = query.value(QStringLiteral("att_sha")).toString();
        attachment.relPath = query.value(QStringLiteral("att_path")).toString();
        attachment.thumbPath = query.value(QStringLiteral("att_thumb")).toString();
        attachment.width = query.value(QStringLiteral("att_width")).toInt();
        attachment.height = query.value(QStringLiteral("att_height")).toInt();
        attachment.durationMs = query.value(QStringLiteral("att_duration")).toInt();
        message.attachment = attachment;
    }

    return message;
}

} // namespace

MessageRepository::MessageRepository(QSqlDatabase db)
    : m_db(std::move(db))
{
}

bool MessageRepository::append(const Message &message, Message *stored, QString *error)
{
    Message existing;
    bool found = false;
    if (!findByClientId(message.senderId, message.clientMsgId, &existing, &found, error))
        return false;

    if (found) {
        if (stored)
            *stored = existing;
        return true;
    }

    if (!m_db.transaction())
        return sql::fail(error, QStringLiteral("Не удалось начать транзакцию сообщения"));

    QSqlQuery maxQuery(m_db);
    maxQuery.prepare(QStringLiteral(
        "SELECT COALESCE(MAX(seq), 0) FROM messages WHERE chat_id = ?"));
    maxQuery.addBindValue(message.chatId);

    if (!maxQuery.exec() || !maxQuery.next()) {
        m_db.rollback();
        return sql::failQuery(error, maxQuery, QStringLiteral("Выдача seq"));
    }

    const qint64 seq = maxQuery.value(0).toLongLong() + 1;

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral(
        "INSERT INTO messages(chat_id, sender_id, seq, client_msg_id, kind, body, created_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?)"));
    insert.addBindValue(message.chatId);
    insert.addBindValue(message.senderId);
    insert.addBindValue(seq);
    insert.addBindValue(message.clientMsgId);
    insert.addBindValue(messageKindToString(message.kind));
    insert.addBindValue(message.body);
    insert.addBindValue(message.createdAt > 0 ? message.createdAt
                                              : QDateTime::currentMSecsSinceEpoch());

    if (!insert.exec()) {
        m_db.rollback();
        return sql::failQuery(error, insert, QStringLiteral("Сохранение сообщения"));
    }

    const qint64 id = insert.lastInsertId().toLongLong();

    if (!m_db.commit()) {
        m_db.rollback();
        return sql::failQuery(error, insert, QStringLiteral("Фиксация сообщения"));
    }

    if (stored) {
        Message result = message;
        result.id = id;
        result.seq = seq;
        *stored = result;
    }
    return true;
}

bool MessageRepository::store(const Message &message, bool *inserted, QString *error)
{
    // Клиентская БД наполняется из сети: сначала гарантируем существование
    // связанных записей, иначе внешние ключи отклонят вставку.
    QSqlQuery chatRef(m_db);
    chatRef.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO chats(id, kind, title, created_at) VALUES(?, 'direct', NULL, ?)"));
    chatRef.addBindValue(message.chatId);
    chatRef.addBindValue(message.createdAt);
    if (!chatRef.exec()) {
        // Чат мог быть создан между вызовами — не считаем это фатальным, проверим ниже.
        qWarning().noquote() << "chat ref:" << chatRef.lastError().text();
    }

    QSqlQuery userRef(m_db);
    userRef.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO users(id, username, display_name, avatar_hue, created_at) "
        "VALUES(?, ?, ?, ?, ?)"));
    userRef.addBindValue(message.senderId);
    userRef.addBindValue(QStringLiteral("user%1").arg(message.senderId));
    userRef.addBindValue(message.senderDisplayName);
    userRef.addBindValue(message.senderHue);
    userRef.addBindValue(message.createdAt);
    if (!userRef.exec())
        qWarning().noquote() << "user ref:" << userRef.lastError().text();

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO messages(id, chat_id, sender_id, seq, client_msg_id, kind, body, "
        "created_at, deleted, edited_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET body = excluded.body, "
        "deleted = excluded.deleted, edited_at = excluded.edited_at"));
    query.addBindValue(message.id);
    query.addBindValue(message.chatId);
    query.addBindValue(message.senderId);
    query.addBindValue(message.seq);
    query.addBindValue(message.clientMsgId);
    query.addBindValue(messageKindToString(message.kind));
    query.addBindValue(message.body);
    query.addBindValue(message.createdAt);
    query.addBindValue(message.deleted ? 1 : 0);
    query.addBindValue(message.editedAt);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Сохранение сообщения"));

    if (inserted)
        *inserted = (query.numRowsAffected() > 0);
    return true;
}

bool MessageRepository::findByClientId(qint64 senderId, const QString &clientMsgId, Message *out,
                                       bool *found, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QString::fromLatin1(kSelectMessage)
                  + QStringLiteral("WHERE m.sender_id = ? AND m.client_msg_id = ? LIMIT 1"));
    query.addBindValue(senderId);
    query.addBindValue(clientMsgId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Поиск сообщения"));

    const bool exists = query.next();
    if (found)
        *found = exists;
    if (exists && out)
        *out = readMessage(query);
    return true;
}

bool MessageRepository::byId(qint64 id, Message *out, bool *found, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QString::fromLatin1(kSelectMessage) + QStringLiteral("WHERE m.id = ?"));
    query.addBindValue(id);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Поиск сообщения"));

    const bool exists = query.next();
    if (found)
        *found = exists;
    if (exists && out)
        *out = readMessage(query);
    return true;
}

bool MessageRepository::page(qint64 chatId, qint64 beforeSeq, int limit, QList<Message> *out,
                             bool *hasMore, QString *error) const
{
    const int pageSize = qBound(1, limit, 500);

    QString sql = QString::fromLatin1(kSelectMessage);
    sql += QStringLiteral("WHERE m.chat_id = ?");
    if (beforeSeq > 0)
        sql += QStringLiteral(" AND m.seq < ?");
    sql += QStringLiteral(" ORDER BY m.seq DESC LIMIT ?");

    QSqlQuery query(m_db);
    query.prepare(sql);
    query.addBindValue(chatId);
    if (beforeSeq > 0)
        query.addBindValue(beforeSeq);
    query.addBindValue(pageSize + 1);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("История сообщений"));

    QList<Message> messages;
    while (query.next())
        messages.append(readMessage(query));

    const bool more = messages.size() > pageSize;
    if (more)
        messages.removeLast();

    std::reverse(messages.begin(), messages.end());

    if (hasMore)
        *hasMore = more;
    if (out)
        *out = messages;
    return true;
}

bool MessageRepository::lastInChat(qint64 chatId, Message *out, bool *found, QString *error) const
{
    QList<Message> messages;
    bool more = false;
    if (!page(chatId, 0, 1, &messages, &more, error))
        return false;

    const bool exists = !messages.isEmpty();
    if (found)
        *found = exists;
    if (exists && out)
        *out = messages.first();
    return true;
}

bool MessageRepository::maxSeq(qint64 chatId, qint64 *value, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT COALESCE(MAX(seq), 0) FROM messages WHERE chat_id = ?"));
    query.addBindValue(chatId);

    if (!query.exec() || !query.next())
        return sql::failQuery(error, query, QStringLiteral("Максимальный seq"));

    if (value)
        *value = query.value(0).toLongLong();
    return true;
}

bool MessageRepository::search(const QString &query, int limit, QList<Message> *out,
                               QString *error) const
{
    const QString trimmed = query.trimmed();
    if (trimmed.size() < 2)
        return true;

    QString pattern = trimmed;
    pattern.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    pattern.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    pattern.replace(QLatin1Char('_'), QStringLiteral("\\_"));

    QString sql = QString::fromLatin1(kSelectMessage);
    sql += QStringLiteral("WHERE m.body LIKE ? ESCAPE '\\' ORDER BY m.created_at DESC LIMIT ?");

    QSqlQuery statement(m_db);
    statement.prepare(sql);
    statement.addBindValue(QStringLiteral("%") + pattern + QStringLiteral("%"));
    statement.addBindValue(qBound(1, limit, 200));

    if (!statement.exec())
        return sql::failQuery(error, statement, QStringLiteral("Поиск по сообщениям"));

    if (out) {
        out->clear();
        while (statement.next())
            out->append(readMessage(statement));
    }
    return true;
}

bool MessageRepository::countInChat(qint64 chatId, int *value, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM messages WHERE chat_id = ?"));
    query.addBindValue(chatId);

    if (!query.exec() || !query.next())
        return sql::failQuery(error, query, QStringLiteral("Количество сообщений"));

    if (value)
        *value = query.value(0).toInt();
    return true;
}

bool MessageRepository::removeChat(qint64 chatId, QString *error)
{
    QSqlQuery attachments(m_db);
    attachments.prepare(QStringLiteral(
        "DELETE FROM attachments WHERE message_id IN "
        "(SELECT id FROM messages WHERE chat_id = ?)"));
    attachments.addBindValue(chatId);

    if (!attachments.exec())
        return sql::failQuery(error, attachments, QStringLiteral("Удаление вложений чата"));

    QSqlQuery messages(m_db);
    messages.prepare(QStringLiteral("DELETE FROM messages WHERE chat_id = ?"));
    messages.addBindValue(chatId);

    if (!messages.exec())
        return sql::failQuery(error, messages, QStringLiteral("Удаление сообщений чата"));

    return true;
}

bool MessageRepository::updateEdited(qint64 id, const QString &body, qint64 editedAt,
                                     QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE messages SET body = ?, edited_at = ? WHERE id = ?"));
    query.addBindValue(body);
    query.addBindValue(editedAt);
    query.addBindValue(id);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Редактирование сообщения"));

    return true;
}

bool MessageRepository::markDeleted(qint64 id, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE messages SET deleted = 1, body = '', edited_at = 0 WHERE id = ?"));
    query.addBindValue(id);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Удаление сообщения"));

    return true;
}

bool MessageRepository::removeMessage(qint64 id, QString *error)
{
    QSqlQuery attachments(m_db);
    attachments.prepare(QStringLiteral("DELETE FROM attachments WHERE message_id = ?"));
    attachments.addBindValue(id);

    if (!attachments.exec())
        return sql::failQuery(error, attachments, QStringLiteral("Удаление вложения"));

    QSqlQuery message(m_db);
    message.prepare(QStringLiteral("DELETE FROM messages WHERE id = ?"));
    message.addBindValue(id);

    if (!message.exec())
        return sql::failQuery(error, message, QStringLiteral("Удаление сообщения"));

    return true;
}

} // namespace data
