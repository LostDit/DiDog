#include "data/ReceiptRepository.h"

#include "data/SqlUtil.h"

#include <QSqlQuery>
#include <QVariant>

namespace data {

ReceiptRepository::ReceiptRepository(QSqlDatabase db)
    : m_db(std::move(db))
{
}

bool ReceiptRepository::add(qint64 messageId, qint64 userId, const QString &state, qint64 ts,
                            QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO receipts(message_id, user_id, state, ts) VALUES(?, ?, ?, ?)"));
    query.addBindValue(messageId);
    query.addBindValue(userId);
    query.addBindValue(state);
    query.addBindValue(ts);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Сохранение квитанции"));
    return true;
}

bool ReceiptRepository::exists(qint64 messageId, qint64 userId, const QString &state,
                               bool *found, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM receipts WHERE message_id = ? AND user_id = ? AND state = ? LIMIT 1"));
    query.addBindValue(messageId);
    query.addBindValue(userId);
    query.addBindValue(state);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Проверка квитанции"));

    if (found)
        *found = query.next();
    return true;
}

bool ReceiptRepository::stateForMessage(qint64 messageId, qint64 senderId, DeliveryState *state,
                                        QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT "
        " (SELECT COUNT(*) FROM chat_members cm "
        "   WHERE cm.chat_id = m.chat_id AND cm.user_id <> m.sender_id), "
        " (SELECT COUNT(DISTINCT r.user_id) FROM receipts r "
        "   WHERE r.message_id = m.id AND r.state = 'delivered' AND r.user_id <> m.sender_id), "
        " (SELECT COUNT(DISTINCT r.user_id) FROM receipts r "
        "   WHERE r.message_id = m.id AND r.state = 'read' AND r.user_id <> m.sender_id) "
        "FROM messages m WHERE m.id = ? AND m.sender_id = ?"));
    query.addBindValue(messageId);
    query.addBindValue(senderId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Статус сообщения"));

    if (!query.next()) {
        if (state)
            *state = DeliveryState::Sent;
        return true;
    }

    const int peers = query.value(0).toInt();
    const int delivered = query.value(1).toInt();
    const int read = query.value(2).toInt();

    DeliveryState result = DeliveryState::Sent;
    if (peers > 0 && read >= peers)
        result = DeliveryState::Read;
    else if (peers > 0 && delivered >= peers)
        result = DeliveryState::Delivered;

    if (state)
        *state = result;
    return true;
}

} // namespace data
