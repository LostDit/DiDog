#pragma once

#include "data/Models.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace data {

// Очередь сообщений, отправленных без соединения (только на клиенте).
class OutboxRepository
{
public:
    explicit OutboxRepository(QSqlDatabase db);

    bool add(const OutboxEntry &entry, QString *error);
    bool pending(qint64 chatId, QList<OutboxEntry> *out, QString *error) const;
    bool allPending(QList<OutboxEntry> *out, QString *error) const;
    bool remove(const QString &clientMsgId, QString *error);
    bool markFailed(const QString &clientMsgId, int attempts, QString *error);
    bool count(int *value, QString *error) const;

private:
    QSqlDatabase m_db;
};

} // namespace data
