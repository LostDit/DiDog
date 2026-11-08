#pragma once

#include "data/Models.h"
#include "security/PasswordHasher.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace data {

class UserRepository
{
public:
    explicit UserRepository(QSqlDatabase db);

    bool insert(const QString &username, const QString &displayName,
                const security::PasswordHash &password, int avatarHue,
                User *created, QString *error);

    bool findById(qint64 id, User *out, QString *error) const;
    bool findByUsername(const QString &username, User *out, QString *error) const;

    // Для входа: пользователь + сохранённый хеш пароля.
    bool findAuth(const QString &username, User *out, security::PasswordHash *hash,
                  QString *error) const;

    bool usernameExists(const QString &username, bool *exists, QString *error) const;
    bool count(int *value, QString *error) const;
    bool all(QList<User> *users, QString *error) const;

    // Поиск по логину и имени для приглашения в группу.
    bool search(const QString &query, qint64 excludeId, int limit, QList<User> *users,
                QString *error) const;

private:
    QSqlDatabase m_db;
};

} // namespace data
