#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace data {

// Закреплённые сообщения чата.
class PinRepository
{
public:
    explicit PinRepository(QSqlDatabase db);

    bool set(qint64 chatId, qint64 messageId, qint64 pinnedBy, bool on, QString *error);
    bool ids(qint64 chatId, QList<qint64> *out, QString *error) const;
    bool isPinned(qint64 chatId, qint64 messageId, bool *pinned, QString *error) const;

private:
    QSqlDatabase m_db;
};

} // namespace data
