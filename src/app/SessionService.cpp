#include "app/SessionService.h"

#include "app/SettingsStore.h"
#include "core/Protocol.h"
#include "data/JsonMapping.h"

#include <QDebug>
#include <QJsonObject>

namespace app {

SessionService::SessionService(net::ClientConnection *connection, SettingsStore *settings,
                               QObject *parent)
    : QObject(parent)
    , m_connection(connection)
    , m_settings(settings)
{
    if (!m_connection)
        return;

    connect(m_connection, &net::ClientConnection::socketConnected,
            this, &SessionService::onSocketConnected);

    connect(m_connection, &net::ClientConnection::stateChanged, this,
            [this](net::ClientConnection::State state) {
                emit connectionStateChanged(state);
            });
}

void SessionService::login(const QString &username, const QString &password, bool remember)
{
    m_pendingRemember = remember;

    QJsonObject data;
    data.insert(QStringLiteral("username"), username);
    data.insert(QStringLiteral("password"), password);

    m_connection->request(QStringLiteral("auth.login"), data, [this, remember](const QJsonObject &reply) {
        applyAuthReply(reply, remember, true);
    });
}

void SessionService::registerUser(const QString &username, const QString &displayName,
                                  const QString &password)
{
    m_pendingRemember = true;

    QJsonObject data;
    data.insert(QStringLiteral("username"), username);
    data.insert(QStringLiteral("display_name"), displayName);
    data.insert(QStringLiteral("password"), password);

    m_connection->request(QStringLiteral("auth.register"), data,
                          [this](const QJsonObject &reply) {
                              applyAuthReply(reply, true, false);
                          });
}

void SessionService::logout()
{
    if (m_connection && m_connection->isConnected())
        m_connection->request(QStringLiteral("auth.logout"), QJsonObject(), nullptr, 5000);

    m_authenticated = false;
    m_token.clear();
    m_user = data::User();

    if (m_settings) {
        m_settings->setAuthToken(QString());
        m_settings->sync();
    }

    emit loggedOut();
}

void SessionService::resumeIfPossible()
{
    if (!m_settings || !m_connection || !m_connection->isConnected())
        return;

    const QString token = m_settings->authToken();
    if (token.isEmpty())
        return;

    QJsonObject data;
    data.insert(QStringLiteral("token"), token);

    m_connection->request(QStringLiteral("auth.resume"), data, [this](const QJsonObject &reply) {
        if (reply.value(QStringLiteral("ok")).toBool(false)) {
            applyAuthReply(reply, true, false);
            return;
        }

        // Токен просрочен — молча забываем его, пользователь войдёт заново.
        if (m_settings) {
            m_settings->setAuthToken(QString());
            m_settings->sync();
        }
        qInfo() << "Сохранённая сессия недействительна";
    });
}

void SessionService::onSocketConnected()
{
    resumeIfPossible();
}

void SessionService::applyAuthReply(const QJsonObject &reply, bool remember, bool isLogin)
{
    if (!reply.value(QStringLiteral("ok")).toBool(false)) {
        const QString message = core::proto::errorMessageOf(reply);
        qWarning().noquote() << QStringLiteral("Ошибка авторизации: %1").arg(message);
        emit authFailed(message);
        return;
    }

    const QJsonObject data = reply.value(QStringLiteral("data")).toObject();
    m_token = data.value(QStringLiteral("token")).toString();
    m_user = data::json::userFromJson(data.value(QStringLiteral("user")).toObject());
    m_authenticated = true;

    rememberToken(m_token, remember);

    if (m_settings && isLogin)
        m_settings->setLastUserName(m_user.username);

    qInfo().noquote() << QStringLiteral("Сессия открыта: %1 (#%2)")
                             .arg(m_user.username)
                             .arg(m_user.id);

    emit authenticated(m_user);
}

void SessionService::rememberToken(const QString &token, bool remember)
{
    if (!m_settings)
        return;

    m_settings->setAuthToken(remember ? token : QString());
    m_settings->sync();
}

} // namespace app
