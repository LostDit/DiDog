#pragma once

#include "data/Models.h"

#include <QObject>
#include <QScopedPointer>

namespace ui {
class LoginWindow;
class MainWindow;
} // namespace ui

namespace net {
class ClientConnection;
} // namespace net

namespace app {

class ChatService;
class HistoryStore;
class SessionService;
class SettingsStore;
class ThemeManager;

// Связывает окна и сервисы: вход → главное окно → выход → снова вход.
class ClientController : public QObject
{
    Q_OBJECT

public:
    ClientController(SettingsStore *settings, ThemeManager *theme, QObject *parent = nullptr);
    ~ClientController() override;

    void start();

private slots:
    void onAuthenticated(const data::User &user);
    void onLoggedOut();

private:
    void createConnection();

    SettingsStore *m_settings = nullptr;
    ThemeManager *m_theme = nullptr;
    net::ClientConnection *m_connection = nullptr;
    HistoryStore *m_history = nullptr;
    ChatService *m_chats = nullptr;
    SessionService *m_session = nullptr;
    QScopedPointer<ui::LoginWindow> m_loginWindow;
    QScopedPointer<ui::MainWindow> m_mainWindow;
};

} // namespace app
