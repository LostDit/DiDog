#include "data/ChatRepository.h"

#include "data/SqlUtil.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

namespace data {
namespace {

Chat readChat(const QSqlQuery &query)
{
    Chat chat;
    chat.id = query.value(QStringLiteral("id")).toLongLong();
    chat.kind = query.value(QStringLiteral("kind")).toString();
    chat.title = query.value(QStringLiteral("title")).toString();
    chat.createdAt = query.value(QStringLiteral("created_at")).toLongLong();
    return chat;
}

} // namespace

ChatRepository::ChatRepository(QSqlDatabase db)
    : m_db(std::move(db))
{
}

bool ChatRepository::createGroup(const QString &title, Chat *created, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO chats(kind, title, created_at) VALUES('group', ?, ?)"));
    query.addBindValue(title);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Не удалось создать чат"));

    if (created) {
        created->id = query.lastInsertId().toLongLong();
        created->kind = QStringLiteral("group");
        created->title = title;
        created->createdAt = QDateTime::currentMSecsSinceEpoch();
    }
    return true;
}

bool ChatRepository::createDirect(qint64 userA, qint64 userB, Chat *created, QString *error)
{
    Chat existing;
    bool found = false;
    if (!findDirect(userA, userB, &existing, &found, error))
        return false;

    if (found) {
        if (created)
            *created = existing;
        return true;
    }

    if (!m_db.transaction())
        return sql::fail(error, QStringLiteral("Не удалось начать транзакцию"));

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO chats(kind, title, created_at) VALUES('direct', NULL, ?)"));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());

    if (!query.exec()) {
        m_db.rollback();
        return sql::failQuery(error, query, QStringLiteral("Не удалось создать чат"));
    }

    const qint64 chatId = query.lastInsertId().toLongLong();

    if (!addMember(chatId, userA, error) || !addMember(chatId, userB, error)) {
        m_db.rollback();
        return false;
    }

    if (!m_db.commit()) {
        m_db.rollback();
        return sql::failQuery(error, query, QStringLiteral("Не удалось зафиксировать чат"));
    }

    if (created) {
        created->id = chatId;
        created->kind = QStringLiteral("direct");
        created->title.clear();
    }
    return true;
}

bool ChatRepository::findDirect(qint64 userA, qint64 userB, Chat *out, bool *found,
                                QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT c.id, c.kind, c.title, c.created_at "
        "FROM chats c "
        "JOIN chat_members a ON a.chat_id = c.id AND a.user_id = ? "
        "JOIN chat_members b ON b.chat_id = c.id AND b.user_id = ? "
        "WHERE c.kind = 'direct' LIMIT 1"));
    query.addBindValue(userA);
    query.addBindValue(userB);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Поиск личного чата"));

    const bool exists = query.next();
    if (found)
        *found = exists;
    if (exists && out)
        *out = readChat(query);
    return true;
}

bool ChatRepository::addMember(qint64 chatId, qint64 userId, QString *error)
{
    return addMember(chatId, userId, QStringLiteral("member"), error);
}

bool ChatRepository::addMember(qint64 chatId, qint64 userId, const QString &role, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO chat_members(chat_id, user_id, last_read_seq, joined_at, role) "
        "VALUES(?, ?, 0, ?, ?)"));
    query.addBindValue(chatId);
    query.addBindValue(userId);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    query.addBindValue(role);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Не удалось добавить участника"));
    return true;
}

bool ChatRepository::members(qint64 chatId, QList<ChatMember> *out, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT u.id, u.username, u.display_name, u.avatar_hue, cm.role "
        "FROM chat_members cm JOIN users u ON u.id = cm.user_id "
        "WHERE cm.chat_id = ? "
        "ORDER BY CASE cm.role WHEN 'owner' THEN 0 WHEN 'admin' THEN 1 ELSE 2 END, "
        "         u.display_name COLLATE NOCASE"));
    query.addBindValue(chatId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Список участников"));

    if (out) {
        out->clear();
        while (query.next()) {
            ChatMember member;
            member.userId = query.value(0).toLongLong();
            member.username = query.value(1).toString();
            member.displayName = query.value(2).toString();
            member.avatarHue = query.value(3).toInt();
            member.role = query.value(4).toString();
            out->append(member);
        }
    }
    return true;
}

bool ChatRepository::memberRole(qint64 chatId, qint64 userId, QString *role, bool *found,
                                QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT role FROM chat_members WHERE chat_id = ? AND user_id = ?"));
    query.addBindValue(chatId);
    query.addBindValue(userId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Роль участника"));

    if (found)
        *found = query.next();
    if (found && *found && role)
        *role = query.value(0).toString();
    return true;
}

bool ChatRepository::removeMember(qint64 chatId, qint64 userId, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "DELETE FROM chat_members WHERE chat_id = ? AND user_id = ?"));
    query.addBindValue(chatId);
    query.addBindValue(userId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Выход из чата"));
    return true;
}

bool ChatRepository::setRole(qint64 chatId, qint64 userId, const QString &role, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE chat_members SET role = ? WHERE chat_id = ? AND user_id = ?"));
    query.addBindValue(role);
    query.addBindValue(chatId);
    query.addBindValue(userId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Смена роли участника"));
    return true;
}

bool ChatRepository::deleteChat(qint64 chatId, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM chats WHERE id = ?"));
    query.addBindValue(chatId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Удаление чата"));
    return true;
}

bool ChatRepository::isMember(qint64 chatId, qint64 userId, bool *isMember, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM chat_members WHERE chat_id = ? AND user_id = ? LIMIT 1"));
    query.addBindValue(chatId);
    query.addBindValue(userId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Проверка участия"));

    if (isMember)
        *isMember = query.next();
    return true;
}

bool ChatRepository::memberIds(qint64 chatId, QList<qint64> *ids, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT user_id FROM chat_members WHERE chat_id = ?"));
    query.addBindValue(chatId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Участники чата"));

    if (ids) {
        ids->clear();
        while (query.next())
            ids->append(query.value(0).toLongLong());
    }
    return true;
}

bool ChatRepository::ensureGeneralChat(Chat *chat, QString *error)
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT id, kind, title, created_at FROM chats "
            "WHERE kind = 'group' ORDER BY id LIMIT 1"))) {
        return sql::failQuery(error, query, QStringLiteral("Поиск общего чата"));
    }

    if (query.next()) {
        if (chat)
            *chat = readChat(query);
        return true;
    }

    Chat created;
    if (!createGroup(QStringLiteral("Общий чат"), &created, error))
        return false;

    QSqlQuery insertAll(m_db);
    insertAll.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO chat_members(chat_id, user_id, last_read_seq, joined_at) "
        "SELECT ?, id, 0, ? FROM users"));
    insertAll.addBindValue(created.id);
    insertAll.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!insertAll.exec())
        return sql::failQuery(error, insertAll, QStringLiteral("Подключение к общему чату"));

    if (chat)
        *chat = created;
    return true;
}

bool ChatRepository::summaryFor(qint64 chatId, qint64 userId, ChatSummary *out,
                                QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT c.id, c.kind, c.title, c.created_at, cm.last_read_seq "
        "FROM chats c JOIN chat_members cm ON cm.chat_id = c.id "
        "WHERE c.id = ? AND cm.user_id = ?"));
    query.addBindValue(chatId);
    query.addBindValue(userId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Чтение чата"));
    if (!query.next())
        return sql::fail(error, QStringLiteral("Чат не найден"));

    ChatSummary summary;
    summary.chat = readChat(query);
    const qint64 lastRead = query.value(QStringLiteral("last_read_seq")).toLongLong();

    if (!fillPeer(&summary, userId)) {
        if (error)
            *error = QStringLiteral("Не удалось определить собеседника");
        return false;
    }

    QSqlQuery last(m_db);
    last.prepare(QStringLiteral(
        "SELECT kind, body, created_at, seq FROM messages "
        "WHERE chat_id = ? ORDER BY seq DESC LIMIT 1"));
    last.addBindValue(chatId);
    if (!last.exec())
        return sql::failQuery(error, last, QStringLiteral("Последнее сообщение"));

    if (last.next()) {
        summary.lastKind = messageKindFromString(last.value(0).toString());
        summary.lastBody = last.value(1).toString();
        summary.lastAt = last.value(2).toLongLong();
        summary.lastSeq = last.value(3).toLongLong();
    } else {
        summary.lastAt = summary.chat.createdAt;
        summary.lastSeq = 0;
    }

    QSqlQuery unread(m_db);
    unread.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM messages "
        "WHERE chat_id = ? AND seq > ? AND sender_id <> ?"));
    unread.addBindValue(chatId);
    unread.addBindValue(lastRead);
    unread.addBindValue(userId);
    if (!unread.exec())
        return sql::failQuery(error, unread, QStringLiteral("Подсчёт непрочитанных"));

    if (unread.next())
        summary.unread = unread.value(0).toInt();

    if (out)
        *out = summary;
    return true;
}

bool ChatRepository::summaries(qint64 userId, QList<ChatSummary> *out, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT c.id, c.kind, c.title, c.created_at, cm.last_read_seq "
        "FROM chats c JOIN chat_members cm ON cm.chat_id = c.id "
        "WHERE cm.user_id = ?"));
    query.addBindValue(userId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Список чатов"));

    QList<ChatSummary> result;
    QList<qint64> chatIds;
    while (query.next())
        chatIds.append(query.value(QStringLiteral("id")).toLongLong());

    for (const qint64 chatId : chatIds) {
        ChatSummary summary;
        if (!summaryFor(chatId, userId, &summary, error))
            return false;
        result.append(summary);
    }

    std::sort(result.begin(), result.end(), [](const ChatSummary &a, const ChatSummary &b) {
        return a.lastAt > b.lastAt;
    });

    if (out)
        *out = result;
    return true;
}

bool ChatRepository::setLastRead(qint64 chatId, qint64 userId, qint64 seq, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE chat_members SET last_read_seq = MAX(last_read_seq, ?) "
        "WHERE chat_id = ? AND user_id = ?"));
    query.addBindValue(seq);
    query.addBindValue(chatId);
    query.addBindValue(userId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Отметка о прочтении"));
    return true;
}

bool ChatRepository::lastReadSeq(qint64 chatId, qint64 userId, qint64 *seq, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT last_read_seq FROM chat_members WHERE chat_id = ? AND user_id = ?"));
    query.addBindValue(chatId);
    query.addBindValue(userId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Чтение отметки"));
    if (query.next() && seq)
        *seq = query.value(0).toLongLong();
    return true;
}

bool ChatRepository::fillPeer(ChatSummary *summary, qint64 viewerId) const
{
    if (summary->chat.kind == QLatin1String("group")) {
        summary->peerDisplayName = summary->chat.title;
        return true;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT u.id, u.display_name, u.avatar_hue "
        "FROM chat_members cm JOIN users u ON u.id = cm.user_id "
        "WHERE cm.chat_id = ? AND cm.user_id <> ? LIMIT 1"));
    query.addBindValue(summary->chat.id);
    query.addBindValue(viewerId);

    if (!query.exec() || !query.next())
        return false;

    summary->peerId = query.value(0).toLongLong();
    summary->peerDisplayName = query.value(1).toString();
    summary->peerHue = query.value(2).toInt();
    return true;
}

} // namespace data
