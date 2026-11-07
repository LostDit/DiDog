#include "app/ClientController.h"

#include "app/AppPaths.h"
#include "app/ChatService.h"
#include "app/HistoryStore.h"
#include "app/SessionService.h"
#include "app/SettingsStore.h"
#include "app/ThemeManager.h"
#include "net/ClientConnection.h"
#include "ui/LoginWindow.h"
#include "ui/MainWindow.h"

#include <QDebug>

namespace app {

ClientController::ClientController(SettingsStore *settings, ThemeManager *theme, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_theme(theme)
{
    createConnection();

    m_history = new HistoryStore(this);
    QString historyError;
    if (!m_history->open(paths::clientDbPath(), &historyError)) {
        qWarning().noquote() << QStringLiteral("Локальная история недоступна: %1")
                                    .arg(historyError);
        delete m_history;
        m_history = nullptr;
    }

    if (m_history) {
        connect(m_history, &HistoryStore::failed, this, [](const QString &message) {
            qWarning().noquote() << QStringLiteral("История: %1").arg(message);
        });
    }
}

ClientController::~ClientController() = default;

void ClientController::createConnection()
{
    m_connection = new net::ClientConnection(this);
    m_connection->setEndpoint(m_settings->serverHost(), m_settings->serverPort());
    m_connection->setAutoReconnect(true);

    m_session = new SessionService(m_connection, m_settings, this);

    connect(m_session, &SessionService::authenticated, this, &ClientController::onAuthenticated);
    connect(m_session, &SessionService::loggedOut, this, &ClientController::onLoggedOut);
}

void ClientController::start()
{
    m_loginWindow.reset(new ui::LoginWindow(m_connection, m_session, m_settings, m_theme));
    m_loginWindow->prepare();
    m_loginWindow->show();
}

void ClientController::onAuthenticated(const data::User &user)
{
    if (!m_chats)
        m_chats = new ChatService(m_connection, m_history, this);

    m_chats->setCurrentUser(user);
    m_chats->loadChats();

    if (!m_mainWindow) {
        m_mainWindow.reset(new ui::MainWindow(m_chats, m_session, m_connection,
                                              m_settings, m_theme));
        m_mainWindow->setUser(user);
        m_mainWindow->show();
    }

    if (m_loginWindow)
        m_loginWindow->hide();

    qInfo().noquote() << QStringLiteral("Открыто окно чатов для %1").arg(user.username);
}

void ClientController::onLoggedOut()
{
    m_mainWindow.reset();

    if (m_chats) {
        m_chats->deleteLater();
        m_chats = nullptr;
    }

    if (m_loginWindow) {
        m_loginWindow->show();
        m_loginWindow->raise();
        m_loginWindow->activateWindow();
    }

    if (m_connection && !m_connection->isConnected())
        m_connection->connectToServer();
}

} // namespace app
