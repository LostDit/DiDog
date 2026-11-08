#include "data/SubscriptionRepository.h"

#include "data/SqlUtil.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QVariant>

namespace data {

SubscriptionRepository::SubscriptionRepository(QSqlDatabase db)
    : m_db(std::move(db))
{
}

bool SubscriptionRepository::current(qint64 userId, QString *plan, qint64 *expiresAt,
                                     QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT plan, expires_at FROM subscriptions WHERE user_id = ?"));
    query.addBindValue(userId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Чтение подписки"));

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!query.next() || query.value(1).toLongLong() <= now) {
        if (plan)
            *plan = QStringLiteral("free");
        if (expiresAt)
            *expiresAt = 0;
        return true;
    }

    if (plan)
        *plan = query.value(0).toString();
    if (expiresAt)
        *expiresAt = query.value(1).toLongLong();
    return true;
}

bool SubscriptionRepository::activate(qint64 userId, const QString &plan, int days,
                                      qint64 *expiresAt, QString *error)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 base = [&]() {
        qint64 currentExpiry = 0;
        QString currentPlan;
        if (current(userId, &currentPlan, &currentExpiry, error) && currentExpiry > now)
            return currentExpiry;
        return now;
    }();

    const qint64 expires = base + static_cast<qint64>(days) * 24 * 60 * 60 * 1000;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO subscriptions(user_id, plan, expires_at) VALUES(?, ?, ?) "
        "ON CONFLICT(user_id) DO UPDATE SET plan = excluded.plan, "
        "expires_at = excluded.expires_at"));
    query.addBindValue(userId);
    query.addBindValue(plan);
    query.addBindValue(expires);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Активация подписки"));

    if (expiresAt)
        *expiresAt = expires;
    return true;
}

} // namespace data
