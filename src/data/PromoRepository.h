#pragma once

#include <QSqlDatabase>
#include <QString>

namespace data {

// Промокоды: одноразовые/многоразовые коды, дающие подписку на N дней.
class PromoRepository
{
public:
    explicit PromoRepository(QSqlDatabase db);

    bool create(const QString &code, const QString &plan, int days, int maxUses,
                qint64 expiresAt, QString *error);

    // Активирует код для пользователя. Возвращает план и количество дней.
    bool redeem(const QString &code, qint64 userId, QString *plan, int *days,
                QString *error);

private:
    QSqlDatabase m_db;
};

} // namespace data
