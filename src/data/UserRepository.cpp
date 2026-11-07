#include "data/UserRepository.h"

#include "data/SqlUtil.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QVariant>

namespace data {
namespace {

User readUser(const QSqlQuery &query)
{
    User user;
    user.id = query.value(QStringLiteral("id")).toLongLong();
    user.username = query.value(QStringLiteral("username")).toString();
    user.displayName = query.value(QStringLiteral("display_name")).toString();
    user.avatarHue = query.value(QStringLiteral("avatar_hue")).toInt();
    user.createdAt = query.value(QStringLiteral("created_at")).toLongLong();
    return user;
}

const char kUserColumns[] =
    "id, username, display_name, avatar_hue, created_at";

} // namespace

UserRepository::UserRepository(QSqlDatabase db)
    : m_db(std::move(db))
{
}

bool UserRepository::insert(const QString &username, const QString &displayName,
                            const security::PasswordHash &password, int avatarHue,
                            User *created, QString *error)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO users(username, display_name, pwd_salt, pwd_hash, pwd_iters, "
        "avatar_hue, created_at) VALUES(?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(username);
    query.addBindValue(displayName);
    query.addBindValue(password.salt);
    query.addBindValue(password.hash);
    query.addBindValue(password.iterations);
    query.addBindValue(avatarHue);
    query.addBindValue(now);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Не удалось создать пользователя"));

    if (created) {
        User user;
        user.id = query.lastInsertId().toLongLong();
        user.username = username;
        user.displayName = displayName;
        user.avatarHue = avatarHue;
        user.createdAt = now;
        *created = user;
    }
    return true;
}

bool UserRepository::findById(qint64 id, User *out, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT %1 FROM users WHERE id = ?")
                      .arg(QString::fromLatin1(kUserColumns)));
    query.addBindValue(id);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Поиск пользователя"));
    if (!query.next())
        return sql::fail(error, QStringLiteral("Пользователь не найден"));

    if (out)
        *out = readUser(query);
    return true;
}

bool UserRepository::findByUsername(const QString &username, User *out, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT %1 FROM users WHERE username = ? COLLATE NOCASE")
                      .arg(QString::fromLatin1(kUserColumns)));
    query.addBindValue(username);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Поиск пользователя"));
    if (!query.next())
        return sql::fail(error, QStringLiteral("Пользователь не найден"));

    if (out)
        *out = readUser(query);
    return true;
}

bool UserRepository::findAuth(const QString &username, User *out, security::PasswordHash *hash,
                              QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, username, display_name, avatar_hue, created_at, pwd_salt, pwd_hash, pwd_iters "
        "FROM users WHERE username = ? COLLATE NOCASE"));
    query.addBindValue(username);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Поиск пользователя"));
    if (!query.next())
        return sql::fail(error, QStringLiteral("Пользователь не найден"));

    if (out)
        *out = readUser(query);

    if (hash) {
        hash->salt = query.value(QStringLiteral("pwd_salt")).toByteArray();
        hash->hash = query.value(QStringLiteral("pwd_hash")).toByteArray();
        hash->iterations = query.value(QStringLiteral("pwd_iters")).toInt();
    }
    return true;
}

bool UserRepository::usernameExists(const QString &username, bool *exists, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT 1 FROM users WHERE username = ? COLLATE NOCASE"));
    query.addBindValue(username);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Проверка логина"));

    if (exists)
        *exists = query.next();
    return true;
}

bool UserRepository::count(int *value, QString *error) const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM users")))
        return sql::failQuery(error, query, QStringLiteral("Подсчёт пользователей"));
    if (query.next() && value)
        *value = query.value(0).toInt();
    return true;
}

bool UserRepository::all(QList<User> *users, QString *error) const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT %1 FROM users ORDER BY display_name COLLATE NOCASE")
                        .arg(QString::fromLatin1(kUserColumns)))) {
        return sql::failQuery(error, query, QStringLiteral("Список пользователей"));
    }

    if (users) {
        users->clear();
        while (query.next())
            users->append(readUser(query));
    }
    return true;
}

} // namespace data
