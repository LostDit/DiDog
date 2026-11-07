#pragma once

#include "net/ClientConnection.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace app {
class SessionService;
class SettingsStore;
class ThemeManager;
} // namespace app

namespace ui {

// Экран входа и регистрации.
class LoginWindow : public QWidget
{
    Q_OBJECT

public:
    LoginWindow(net::ClientConnection *connection, app::SessionService *session,
                app::SettingsStore *settings, app::ThemeManager *theme,
                QWidget *parent = nullptr);

    // Подключается к сохранённому серверу и готовит форму.
    void prepare();

signals:
    void authenticated();

private slots:
    void onModeChanged();
    void onSubmit();
    void onAuthFailed(const QString &message);
    void onConnectionStateChanged(net::ClientConnection::State state);

private:
    void buildUi();
    void applyMode();
    void showError(const QString &text);
    void hideError();
    bool ensureConnected();
    void saveEndpoint();

    net::ClientConnection *m_connection = nullptr;
    app::SessionService *m_session = nullptr;
    app::SettingsStore *m_settings = nullptr;
    app::ThemeManager *m_theme = nullptr;

    QPushButton *m_loginTab = nullptr;
    QPushButton *m_registerTab = nullptr;
    QLineEdit *m_username = nullptr;
    QLineEdit *m_displayName = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_remember = nullptr;
    QLineEdit *m_host = nullptr;
    QLineEdit *m_port = nullptr;
    QLabel *m_error = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_submit = nullptr;
    QToolButton *m_themeButton = nullptr;

    bool m_registerMode = false;
};

} // namespace ui
