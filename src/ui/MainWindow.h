#pragma once

#include <QMainWindow>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QToolButton;

namespace app {
class SettingsStore;
class ThemeManager;
} // namespace app

namespace ui {

class AvatarWidget;

// Главное окно: сайдбар со списком чатов + область переписки.
// На этапе M0 — каркас и тема; наполнение появляется на M4.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(app::ThemeManager *theme, app::SettingsStore *settings,
               QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onThemeToggleClicked();

private:
    QWidget *buildSidebar();
    QWidget *buildChatPane();
    QWidget *buildEmptyChatPane();

    void restoreWindowState();
    void updateThemeButton();

    app::ThemeManager *m_theme = nullptr;
    app::SettingsStore *m_settings = nullptr;

    AvatarWidget *m_profileAvatar = nullptr;
    QLabel *m_profileName = nullptr;
    QLabel *m_profileSub = nullptr;
    QToolButton *m_themeButton = nullptr;
    QLineEdit *m_searchField = nullptr;
    QListWidget *m_chatList = nullptr;
    QStackedWidget *m_chatStack = nullptr;
};

} // namespace ui
