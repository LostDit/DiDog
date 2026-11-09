#include "data/ReactionRepository.h"

#include "data/SqlUtil.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QVariant>

namespace data {

ReactionRepository::ReactionRepository(QSqlDatabase db)
    : m_db(std::move(db))
{
}

bool ReactionRepository::set(qint64 messageId, qint64 userId, const QString &emoji,
                             bool on, QString *error)
{
    QSqlQuery query(m_db);
    if (on) {
        query.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO reactions(message_id, user_id, emoji, created_at) "
            "VALUES(?, ?, ?, ?)"));
        query.addBindValue(messageId);
        query.addBindValue(userId);
        query.addBindValue(emoji);
        query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    } else {
        query.prepare(QStringLiteral(
            "DELETE FROM reactions WHERE message_id = ? AND user_id = ? AND emoji = ?"));
        query.addBindValue(messageId);
        query.addBindValue(userId);
        query.addBindValue(emoji);
    }

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Реакция"));
    return true;
}

bool ReactionRepository::counts(qint64 messageId, QHash<QString, int> *out,
                                QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT emoji, COUNT(*) FROM reactions WHERE message_id = ? GROUP BY emoji"));
    query.addBindValue(messageId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Подсчёт реакций"));

    if (out) {
        out->clear();
        while (query.next())
            out->insert(query.value(0).toString(), query.value(1).toInt());
    }
    return true;
}

bool ReactionRepository::byUser(qint64 messageId, qint64 userId, QSet<QString> *out,
                                QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT emoji FROM reactions WHERE message_id = ? AND user_id = ?"));
    query.addBindValue(messageId);
    query.addBindValue(userId);

    if (!query.exec())
        return false;

    if (out) {
        out->clear();
        while (query.next())
            out->insert(query.value(0).toString());
    }
    return true;
}

} // namespace data
