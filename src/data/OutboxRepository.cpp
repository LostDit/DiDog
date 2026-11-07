#include "data/OutboxRepository.h"

#include "data/SqlUtil.h"

#include <QSqlQuery>
#include <QVariant>

namespace data {
namespace {

const char kSelectOutbox[] =
    "SELECT client_msg_id, chat_id, kind, body, attach_path, state, attempts, created_at "
    "FROM outbox ";

OutboxEntry readEntry(const QSqlQuery &query)
{
    OutboxEntry entry;
    entry.clientMsgId = query.value(0).toString();
    entry.chatId = query.value(1).toLongLong();
    entry.kind = messageKindFromString(query.value(2).toString());
    entry.body = query.value(3).toString();
    entry.attachPath = query.value(4).toString();
    entry.attempts = query.value(6).toInt();
    entry.createdAt = query.value(7).toLongLong();

    const QString state = query.value(5).toString();
    if (state == QLatin1String("failed"))
        entry.state = DeliveryState::Failed;
    else if (state == QLatin1String("pending"))
        entry.state = DeliveryState::Pending;
    else
        entry.state = DeliveryState::Sent;

    return entry;
}

} // namespace

OutboxRepository::OutboxRepository(QSqlDatabase db)
    : m_db(std::move(db))
{
}

bool OutboxRepository::add(const OutboxEntry &entry, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO outbox(client_msg_id, chat_id, kind, body, attach_path, state, "
        "attempts, created_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(entry.clientMsgId);
    query.addBindValue(entry.chatId);
    query.addBindValue(messageKindToString(entry.kind));
    query.addBindValue(entry.body);
    query.addBindValue(entry.attachPath);
    query.addBindValue(deliveryStateToString(entry.state));
    query.addBindValue(entry.attempts);
    query.addBindValue(entry.createdAt);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Постановка в очередь"));
    return true;
}

bool OutboxRepository::pending(qint64 chatId, QList<OutboxEntry> *out, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QString::fromLatin1(kSelectOutbox)
                  + QStringLiteral("WHERE chat_id = ? AND state = 'pending' ORDER BY created_at"));
    query.addBindValue(chatId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Чтение очереди"));

    if (out) {
        out->clear();
        while (query.next())
            out->append(readEntry(query));
    }
    return true;
}

bool OutboxRepository::allPending(QList<OutboxEntry> *out, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QString::fromLatin1(kSelectOutbox)
                  + QStringLiteral("WHERE state = 'pending' ORDER BY created_at"));

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Чтение очереди"));

    if (out) {
        out->clear();
        while (query.next())
            out->append(readEntry(query));
    }
    return true;
}

bool OutboxRepository::remove(const QString &clientMsgId, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM outbox WHERE client_msg_id = ?"));
    query.addBindValue(clientMsgId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Удаление из очереди"));
    return true;
}

bool OutboxRepository::markFailed(const QString &clientMsgId, int attempts, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE outbox SET state = 'failed', attempts = ? WHERE client_msg_id = ?"));
    query.addBindValue(attempts);
    query.addBindValue(clientMsgId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Отметка об ошибке"));
    return true;
}

bool OutboxRepository::count(int *value, QString *error) const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM outbox WHERE state = 'pending'")))
        return sql::failQuery(error, query, QStringLiteral("Размер очереди"));

    if (query.next() && value)
        *value = query.value(0).toInt();
    return true;
}

} // namespace data
