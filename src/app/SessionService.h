#pragma once

#include "data/Models.h"
#include "net/ClientConnection.h"

#include <QObject>
#include <QString>

namespace app {

class SettingsStore;

// Управление сессией: регистрация, вход, восстановление по токену, выход.
// Токен хранится в настройках профиля (если пользователь выбрал «запомнить»).
class SessionService : public QObject
{
    Q_OBJECT

public:
    SessionService(net::ClientConnection *connection, SettingsStore *settings,
                   QObject *parent = nullptr);

    bool isAuthenticated() const { return m_authenticated; }
    data::User user() const { return m_user; }
    QString token() const { return m_token; }

    void login(const QString &username, const QString &password, bool remember);
    void registerUser(const QString &username, const QString &displayName,
                      const QString &password);
    void logout();

    // Пробует восстановить сессию сохранённым токеном (вызывается при подключении).
    void resumeIfPossible();

signals:
    void authenticated(const data::User &user);
    void authFailed(const QString &message);
    void loggedOut();
    void connectionStateChanged(net::ClientConnection::State state);

private slots:
    void onSocketConnected();

private:
    void applyAuthReply(const QJsonObject &reply, bool remember, bool isLogin);
    void rememberToken(const QString &token, bool remember);

    net::ClientConnection *m_connection = nullptr;
    SettingsStore *m_settings = nullptr;
    data::User m_user;
    QString m_token;
    bool m_authenticated = false;
    bool m_pendingRemember = true;
};

} // namespace app
