#pragma once

#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QString>

namespace data {

// Реакции на сообщения: эмодзи, пользователь, время.
class ReactionRepository
{
public:
    explicit ReactionRepository(QSqlDatabase db);

    bool set(qint64 messageId, qint64 userId, const QString &emoji, bool on,
             QString *error);
    bool counts(qint64 messageId, QHash<QString, int> *out, QString *error) const;
    bool byUser(qint64 messageId, qint64 userId, QSet<QString> *out,
                QString *error) const;

private:
    QSqlDatabase m_db;
};

} // namespace data
