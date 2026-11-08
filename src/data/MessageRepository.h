#pragma once

#include "data/Models.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace data {

class MessageRepository
{
public:
    explicit MessageRepository(QSqlDatabase db);

    // Сервер: выдаёт id и seq (внутри транзакции), идемпотентно по client_msg_id.
    bool append(const Message &message, Message *stored, QString *error);

    // Клиент: сохраняет сообщение с id/seq, полученными от сервера.
    bool store(const Message &message, bool *inserted, QString *error);

    bool findByClientId(qint64 senderId, const QString &clientMsgId, Message *out,
                        bool *found, QString *error) const;
    bool byId(qint64 id, Message *out, bool *found, QString *error) const;

    // Keyset-пагинация: beforeSeq <= 0 — последние сообщения.
    bool page(qint64 chatId, qint64 beforeSeq, int limit, QList<Message> *out,
              bool *hasMore, QString *error) const;

    bool lastInChat(qint64 chatId, Message *out, bool *found, QString *error) const;
    bool maxSeq(qint64 chatId, qint64 *value, QString *error) const;
    bool countInChat(qint64 chatId, int *value, QString *error) const;

    // Поиск по тексту сообщений (LIKE, регистронезависимо для ASCII).
    bool search(const QString &query, int limit, QList<Message> *out, QString *error) const;

    // Удаление локальной истории чата (вложения удаляются вместе с сообщениями).
    bool removeChat(qint64 chatId, QString *error);

    // Редактирование и удаление (у себя — локально, у всех — после ответа сервера).
    bool updateEdited(qint64 id, const QString &body, qint64 editedAt, QString *error);
    bool markDeleted(qint64 id, QString *error);
    bool removeMessage(qint64 id, QString *error);

private:
    QSqlDatabase m_db;
};

} // namespace data
