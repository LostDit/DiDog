#include "data/PinRepository.h"

#include "data/SqlUtil.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QVariant>

namespace data {

PinRepository::PinRepository(QSqlDatabase db)
    : m_db(std::move(db))
{
}

bool PinRepository::set(qint64 chatId, qint64 messageId, qint64 pinnedBy, bool on,
                        QString *error)
{
    QSqlQuery query(m_db);
    if (on) {
        query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO pins(chat_id, message_id, pinned_by, created_at) "
            "VALUES(?, ?, ?, ?)"));
        query.addBindValue(chatId);
        query.addBindValue(messageId);
        query.addBindValue(pinnedBy);
        query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    } else {
        query.prepare(QStringLiteral(
            "DELETE FROM pins WHERE chat_id = ? AND message_id = ?"));
        query.addBindValue(chatId);
        query.addBindValue(messageId);
    }

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Закрепление"));
    return true;
}

bool PinRepository::ids(qint64 chatId, QList<qint64> *out, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT message_id FROM pins WHERE chat_id = ? ORDER BY created_at DESC LIMIT 50"));
    query.addBindValue(chatId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Закреплённые сообщения"));

    if (out) {
        out->clear();
        while (query.next())
            out->append(query.value(0).toLongLong());
    }
    return true;
}

bool PinRepository::isPinned(qint64 chatId, qint64 messageId, bool *pinned,
                             QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM pins WHERE chat_id = ? AND message_id = ?"));
    query.addBindValue(chatId);
    query.addBindValue(messageId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Проверка закрепления"));

    if (pinned)
        *pinned = query.next() && query.value(0).toInt() > 0;
    return true;
}

} // namespace data
