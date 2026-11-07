#pragma once

#include "data/Models.h"

#include <QSqlDatabase>
#include <QString>

namespace data {

class ReceiptRepository
{
public:
    explicit ReceiptRepository(QSqlDatabase db);

    bool add(qint64 messageId, qint64 userId, const QString &state, qint64 ts, QString *error);
    bool exists(qint64 messageId, qint64 userId, const QString &state, bool *found,
                QString *error) const;

    // Агрегированный статус своего сообщения: худшее из состояний получателей.
    bool stateForMessage(qint64 messageId, qint64 senderId, DeliveryState *state,
                         QString *error) const;

private:
    QSqlDatabase m_db;
};

} // namespace data
