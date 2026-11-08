#include "data/PromoRepository.h"

#include "data/SqlUtil.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QVariant>

namespace data {

PromoRepository::PromoRepository(QSqlDatabase db)
    : m_db(std::move(db))
{
}

bool PromoRepository::create(const QString &code, const QString &plan, int days, int maxUses,
                             qint64 expiresAt, QString *error)
{
    const QString trimmed = code.trimmed().toUpper();
    if (trimmed.isEmpty() || days <= 0) {
        if (error)
            *error = QStringLiteral("Некорректный промокод");
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO promo_codes(code, plan, days, max_uses, used, expires_at) "
        "VALUES(?, ?, ?, ?, 0, ?)"));
    query.addBindValue(trimmed);
    query.addBindValue(plan);
    query.addBindValue(days);
    query.addBindValue(qMax(0, maxUses));
    query.addBindValue(expiresAt);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Создание промокода"));

    return true;
}

bool PromoRepository::redeem(const QString &code, qint64 userId, QString *plan, int *days,
                             QString *error)
{
    const QString trimmed = code.trimmed().toUpper();
    if (trimmed.isEmpty()) {
        if (error)
            *error = QStringLiteral("Введите промокод");
        return false;
    }

    if (!m_db.transaction()) {
        if (error)
            *error = QStringLiteral("Не удалось начать транзакцию");
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT plan, days, max_uses, used, expires_at FROM promo_codes WHERE code = ?"));
    query.addBindValue(trimmed);

    if (!query.exec()) {
        m_db.rollback();
        return sql::failQuery(error, query, QStringLiteral("Поиск промокода"));
    }

    if (!query.next()) {
        m_db.rollback();
        if (error)
            *error = QStringLiteral("Промокод не найден");
        return false;
    }

    const QString codePlan = query.value(0).toString();
    const int codeDays = query.value(1).toInt();
    const int maxUses = query.value(2).toInt();
    const int used = query.value(3).toInt();
    const qint64 expiresAt = query.value(4).toLongLong();

    if (expiresAt > 0 && expiresAt <= QDateTime::currentMSecsSinceEpoch()) {
        m_db.rollback();
        if (error)
            *error = QStringLiteral("Срок действия промокода истёк");
        return false;
    }

    if (maxUses > 0 && used >= maxUses) {
        m_db.rollback();
        if (error)
            *error = QStringLiteral("Промокод больше не действует");
        return false;
    }

    QSqlQuery redemption(m_db);
    redemption.prepare(QStringLiteral(
        "INSERT INTO promo_redemptions(code, user_id, redeemed_at) VALUES(?, ?, ?)"));
    redemption.addBindValue(trimmed);
    redemption.addBindValue(userId);
    redemption.addBindValue(QDateTime::currentMSecsSinceEpoch());

    if (!redemption.exec()) {
        m_db.rollback();
        if (error)
            *error = QStringLiteral("Вы уже использовали этот промокод");
        return false;
    }

    QSqlQuery increment(m_db);
    increment.prepare(QStringLiteral("UPDATE promo_codes SET used = used + 1 WHERE code = ?"));
    increment.addBindValue(trimmed);

    if (!increment.exec()) {
        m_db.rollback();
        return sql::failQuery(error, increment, QStringLiteral("Учёт промокода"));
    }

    if (!m_db.commit()) {
        m_db.rollback();
        return sql::failQuery(error, query, QStringLiteral("Фиксация промокода"));
    }

    if (plan)
        *plan = codePlan;
    if (days)
        *days = codeDays;
    return true;
}

} // namespace data
