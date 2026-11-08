#include "app/BillingService.h"

#include "app/SettingsStore.h"
#include "core/Protocol.h"
#include "net/ClientConnection.h"

#include <QJsonArray>
#include <QJsonObject>

namespace app {

BillingService::BillingService(net::ClientConnection *connection, SettingsStore *settings,
                               QObject *parent)
    : QObject(parent)
    , m_connection(connection)
    , m_settings(settings)
{
    if (m_settings) {
        m_plan = m_settings->subscriptionPlan();
        m_expiresAt = m_settings->subscriptionExpiresAt();
    }
}

bool BillingService::hasPremium() const
{
    return m_plan != QLatin1String("free");
}

void BillingService::refresh()
{
    if (!m_connection)
        return;

    m_connection->request(QStringLiteral("billing.status"), QJsonObject(),
                          [this](const QJsonObject &reply) {
                              if (!reply.value(QStringLiteral("ok")).toBool(false))
                                  return;

                              const QJsonObject data =
                                  reply.value(QStringLiteral("data")).toObject();
                              applyStatus(data.value(QStringLiteral("plan")).toString(),
                                          static_cast<qint64>(data.value(
                                              QStringLiteral("expires_at")).toDouble()));
                          },
                          10000);
}

void BillingService::redeem(const QString &code)
{
    if (!m_connection || code.trimmed().isEmpty())
        return;

    m_connection->request(QStringLiteral("billing.redeem"),
                          QJsonObject{{QStringLiteral("code"), code.trimmed()}},
                          [this](const QJsonObject &reply) {
                              if (!reply.value(QStringLiteral("ok")).toBool(false)) {
                                  emit promoFailed(core::proto::errorMessageOf(reply));
                                  return;
                              }

                              const QJsonObject data =
                                  reply.value(QStringLiteral("data")).toObject();
                              applyStatus(data.value(QStringLiteral("plan")).toString(),
                                          static_cast<qint64>(data.value(
                                              QStringLiteral("expires_at")).toDouble()));
                              emit promoActivated();
                              loadThemes();
                          },
                          15000);
}

void BillingService::loadThemes()
{
    if (!m_connection)
        return;

    m_connection->request(QStringLiteral("theme.list"), QJsonObject(),
                          [this](const QJsonObject &reply) {
                              if (!reply.value(QStringLiteral("ok")).toBool(false))
                                  return;

                              QList<ThemeInfo> themes;
                              const QJsonArray array = reply.value(QStringLiteral("data"))
                                                           .toObject()
                                                           .value(QStringLiteral("themes"))
                                                           .toArray();
                              for (const QJsonValue &value : array) {
                                  const QJsonObject item = value.toObject();
                                  ThemeInfo info;
                                  info.id = item.value(QStringLiteral("id")).toString();
                                  info.name = item.value(QStringLiteral("name")).toString();
                                  info.premium =
                                      item.value(QStringLiteral("premium")).toBool(false);
                                  info.accent =
                                      item.value(QStringLiteral("accent")).toString();
                                  themes.append(info);
                              }

                              emit themesLoaded(themes);
                          },
                          10000);
}

void BillingService::requestTheme(const QString &id)
{
    if (!m_connection)
        return;

    m_connection->request(QStringLiteral("theme.get"),
                          QJsonObject{{QStringLiteral("id"), id}},
                          [this, id](const QJsonObject &reply) {
                              if (!reply.value(QStringLiteral("ok")).toBool(false)) {
                                  emit themeFailed(core::proto::errorMessageOf(reply));
                                  return;
                              }

                              QHash<QString, QString> tokens;
                              const QJsonObject object = reply.value(QStringLiteral("data"))
                                                             .toObject()
                                                             .value(QStringLiteral("theme"))
                                                             .toObject()
                                                             .value(QStringLiteral("tokens"))
                                                             .toObject();
                              for (auto it = object.constBegin(); it != object.constEnd(); ++it)
                                  tokens.insert(it.key(), it.value().toString());

                              emit themeReady(id, tokens);
                          },
                          10000);
}

void BillingService::applyStatus(const QString &plan, qint64 expiresAt)
{
    m_plan = plan.isEmpty() ? QStringLiteral("free") : plan;
    m_expiresAt = expiresAt;

    if (m_settings) {
        m_settings->setSubscription(m_plan, m_expiresAt);
        m_settings->sync();
    }

    emit statusChanged();
}

} // namespace app
