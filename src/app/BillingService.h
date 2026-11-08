#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

namespace app {

class SettingsStore;

} // namespace app

namespace net {
class ClientConnection;
}

namespace app {

struct ThemeInfo {
    QString id;
    QString name;
    bool premium = false;
    QString accent;
};

// Подписка: статус, промокоды и пакеты оформления с сервера.
class BillingService : public QObject
{
    Q_OBJECT

public:
    BillingService(net::ClientConnection *connection, SettingsStore *settings,
                   QObject *parent = nullptr);

    void refresh();
    void redeem(const QString &code);
    void loadThemes();
    void requestTheme(const QString &id);

    QString plan() const { return m_plan; }
    qint64 expiresAt() const { return m_expiresAt; }
    bool hasPremium() const;

signals:
    void statusChanged();
    void themesLoaded(const QList<app::ThemeInfo> &themes);
    void themeReady(const QString &id, const QHash<QString, QString> &tokens);
    void themeFailed(const QString &message);
    void promoFailed(const QString &message);
    void promoActivated();

private:
    void applyStatus(const QString &plan, qint64 expiresAt);

    net::ClientConnection *m_connection = nullptr;
    SettingsStore *m_settings = nullptr;
    QString m_plan = QStringLiteral("free");
    qint64 m_expiresAt = 0;
};

} // namespace app
