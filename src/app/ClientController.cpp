#include "app/ClientController.h"

#include "app/AppPaths.h"
#include "app/AttachmentService.h"
#include "app/ChatService.h"
#include "app/HistoryStore.h"
#include "app/NotificationService.h"
#include "app/SessionService.h"
#include "app/SettingsStore.h"
#include "app/ThemeManager.h"
#include "net/ClientConnection.h"
#include "ui/LoginWindow.h"
#include "ui/MainWindow.h"

#include <QCoreApplication>
#include <QDebug>

namespace app {
namespace {

QString previewOf(const data::Message &message)
{
    switch (message.kind) {
    case data::MessageKind::Image: return QStringLiteral("📷 Фотография");
    case data::MessageKind::Video: return QStringLiteral("🎬 Видео");
    case data::MessageKind::Gif:   return QStringLiteral("GIF");
    case data::MessageKind::File:  return QStringLiteral("📎 Файл");
    case data::MessageKind::Text:  break;
    }

    const QString text = message.body.simplified();
    return text.size() > 80 ? text.left(80) + QStringLiteral("…") : text;
}

} // namespace

ClientController::ClientController(SettingsStore *settings, ThemeManager *theme, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_theme(theme)
{
    createConnection();

    m_notifications = new NotificationService(m_settings, this);

    connect(m_notifications, &NotificationService::showRequested, this, [this]() {
        if (m_mainWindow) {
            m_mainWindow->show();
            m_mainWindow->activate();
        } else if (m_loginWindow) {
            m_loginWindow->show();
            m_loginWindow->raise();
        }
    });

    connect(m_notifications, &NotificationService::openChatRequested, this,
            [this](qint64 chatId) {
                if (!m_mainWindow)
                    return;
                m_mainWindow->show();
                m_mainWindow->activate();
                m_mainWindow->openChatById(chatId);
            });

    connect(m_notifications, &NotificationService::quitRequested, this,
            []() { QCoreApplication::quit(); });

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
    m_attachments = new AttachmentService(m_connection, paths::attachmentsDir(), this);

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
    if (!m_chats) {
        m_chats = new ChatService(m_connection, m_history, m_attachments, this);

        connect(m_chats, &ChatService::messageArrived, this,
                [this](qint64 chatId, const data::Message &message) {
                    if (!m_mainWindow || !m_notifications)
                        return;

                    if (m_mainWindow->isActiveWindow() && !m_mainWindow->isMinimized()
                        && m_mainWindow->currentChatId() == chatId) {
                        return;
                    }

                    const data::ChatSummary summary = m_chats->chat(chatId);
                    const QString title = summary.peerDisplayName.isEmpty()
                                              ? tr("Новое сообщение")
                                              : summary.peerDisplayName;
                    m_notifications->notify(title, previewOf(message), chatId);
                });

        connect(m_chats, &ChatService::chatsChanged, this, [this]() {
            if (m_notifications)
                m_notifications->setUnreadCount(m_chats->totalUnread());
        });
    }

    m_chats->setCurrentUser(user);
    m_chats->loadChats();

    if (!m_mainWindow) {
        m_mainWindow.reset(new ui::MainWindow(m_chats, m_session, m_connection,
                                              m_settings, m_theme));
        m_mainWindow->setUser(user);
        m_mainWindow->show();

        if (m_notifications)
            m_notifications->setMainWindow(m_mainWindow.data());
    }

    if (m_loginWindow)
        m_loginWindow->hide();

    qInfo().noquote() << QStringLiteral("Открыто окно чатов для %1").arg(user.username);
}

void ClientController::onLoggedOut()
{
    m_mainWindow.reset();

    if (m_notifications) {
        m_notifications->setMainWindow(nullptr);
        m_notifications->setUnreadCount(0);
    }

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
