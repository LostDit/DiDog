#pragma once

#include <QSqlDatabase>
#include <QString>

namespace data {

// Подписка пользователя: plan = free | plus | pro.
class SubscriptionRepository
{
public:
    explicit SubscriptionRepository(QSqlDatabase db);

    // Возвращает текущий план и срок; для отсутствующей/истёкшей подписки — free.
    bool current(qint64 userId, QString *plan, qint64 *expiresAt, QString *error) const;

    // Продлевает подписку на days дней (с текущего срока, если он ещё не истёк).
    bool activate(qint64 userId, const QString &plan, int days, qint64 *expiresAt,
                  QString *error);

private:
    QSqlDatabase m_db;
};

} // namespace data
