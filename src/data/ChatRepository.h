#pragma once

#include "data/Models.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace data {

class ChatRepository
{
public:
    explicit ChatRepository(QSqlDatabase db);

    bool createGroup(const QString &title, Chat *created, QString *error);
    bool createDirect(qint64 userA, qint64 userB, Chat *created, QString *error);

    // Поиск личного чата двух пользователей. Отсутствие чата — не ошибка: ok=true, found=false.
    bool findDirect(qint64 userA, qint64 userB, Chat *out, bool *found, QString *error) const;

    bool addMember(qint64 chatId, qint64 userId, QString *error);
    bool addMember(qint64 chatId, qint64 userId, const QString &role, QString *error);
    bool isMember(qint64 chatId, qint64 userId, bool *isMember, QString *error) const;
    bool memberIds(qint64 chatId, QList<qint64> *ids, QString *error) const;
    bool members(qint64 chatId, QList<ChatMember> *out, QString *error) const;
    bool setMemberFlag(qint64 chatId, qint64 userId, const QString &flag, bool value,
                         QString *error);
    bool memberRole(qint64 chatId, qint64 userId, QString *role, bool *found,
                    QString *error) const;
    bool removeMember(qint64 chatId, qint64 userId, QString *error);
    bool setRole(qint64 chatId, qint64 userId, const QString &role, QString *error);
    bool deleteChat(qint64 chatId, QString *error);

    // «Общий чат» создаётся при первом запуске сервера и подключает всех пользователей.
    bool ensureGeneralChat(Chat *chat, QString *error);

    bool summaryFor(qint64 chatId, qint64 userId, ChatSummary *out, QString *error) const;
    bool summaries(qint64 userId, QList<ChatSummary> *out, QString *error) const;

    bool setLastRead(qint64 chatId, qint64 userId, qint64 seq, QString *error);
    bool lastReadSeq(qint64 chatId, qint64 userId, qint64 *seq, QString *error) const;

private:
    bool fillPeer(ChatSummary *summary, qint64 viewerId) const;

    QSqlDatabase m_db;
};

} // namespace data
