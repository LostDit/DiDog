#pragma once

#include "data/Models.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace data {

// Кэш списка чатов на клиенте: позволяет показать интерфейс до ответа сервера.
class ChatCacheRepository
{
public:
    explicit ChatCacheRepository(QSqlDatabase db);

    bool replaceAll(const QList<ChatSummary> &chats, QString *error);
    bool all(QList<ChatSummary> *chats, QString *error) const;
    bool clear(QString *error);

private:
    QSqlDatabase m_db;
};

} // namespace data
