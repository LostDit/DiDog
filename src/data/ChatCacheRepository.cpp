#include "data/ChatCacheRepository.h"

#include "data/SqlUtil.h"

#include <QSqlQuery>
#include <QVariant>

namespace data {

ChatCacheRepository::ChatCacheRepository(QSqlDatabase db)
    : m_db(std::move(db))
{
}

bool ChatCacheRepository::replaceAll(const QList<ChatSummary> &chats, QString *error)
{
    if (!m_db.transaction())
        return sql::fail(error, QStringLiteral("Не удалось начать транзакцию кэша чатов"));

    QSqlQuery clearQuery(m_db);
    if (!clearQuery.exec(QStringLiteral("DELETE FROM chat_cache"))) {
        m_db.rollback();
        return sql::failQuery(error, clearQuery, QStringLiteral("Очистка кэша чатов"));
    }

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO chat_cache(chat_id, kind, title, peer_id, peer_name, peer_hue, "
        "last_body, last_kind, last_at, last_seq, unread) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    for (const ChatSummary &summary : chats) {
        insert.addBindValue(summary.chat.id);
        insert.addBindValue(summary.chat.kind);
        insert.addBindValue(summary.chat.title);
        insert.addBindValue(summary.peerId);
        insert.addBindValue(summary.peerDisplayName);
        insert.addBindValue(summary.peerHue);
        insert.addBindValue(summary.lastBody);
        insert.addBindValue(messageKindToString(summary.lastKind));
        insert.addBindValue(summary.lastAt);
        insert.addBindValue(summary.lastSeq);
        insert.addBindValue(summary.unread);

        if (!insert.exec()) {
            m_db.rollback();
            return sql::failQuery(error, insert, QStringLiteral("Сохранение кэша чатов"));
        }
    }

    if (!m_db.commit())
        return sql::failQuery(error, insert, QStringLiteral("Фиксация кэша чатов"));

    return true;
}

bool ChatCacheRepository::all(QList<ChatSummary> *chats, QString *error) const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT chat_id, kind, title, peer_id, peer_name, peer_hue, last_body, last_kind, "
            "last_at, last_seq, unread FROM chat_cache"))) {
        return sql::failQuery(error, query, QStringLiteral("Чтение кэша чатов"));
    }

    if (chats) {
        chats->clear();
        while (query.next()) {
            ChatSummary summary;
            summary.chat.id = query.value(0).toLongLong();
            summary.chat.kind = query.value(1).toString();
            summary.chat.title = query.value(2).toString();
            summary.peerId = query.value(3).toLongLong();
            summary.peerDisplayName = query.value(4).toString();
            summary.peerHue = query.value(5).toInt();
            summary.lastBody = query.value(6).toString();
            summary.lastKind = messageKindFromString(query.value(7).toString());
            summary.lastAt = query.value(8).toLongLong();
            summary.lastSeq = query.value(9).toLongLong();
            summary.unread = query.value(10).toInt();
            chats->append(summary);
        }
    }
    return true;
}

bool ChatCacheRepository::clear(QString *error)
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM chat_cache")))
        return sql::failQuery(error, query, QStringLiteral("Очистка кэша чатов"));
    return true;
}

} // namespace data
