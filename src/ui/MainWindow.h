#pragma once

#include "app/ChatService.h"
#include "data/Models.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>

class QLabel;
class QLineEdit;
class QListView;
class QSortFilterProxyModel;
class QStackedWidget;
class QTimer;
class QToolButton;

namespace app {
class SessionService;
class SettingsStore;
class ThemeManager;
} // namespace app

namespace net {
class ClientConnection;
} // namespace net

namespace ui {

class AvatarWidget;
class ChatListModel;
class ComposerWidget;
class MessageView;
class SearchDialog;

// Главное окно: список чатов и переписка.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(app::ChatService *chats, app::SessionService *session,
               net::ClientConnection *connection, app::SettingsStore *settings,
               app::ThemeManager *theme, QWidget *parent = nullptr);
    ~MainWindow() override;

    void setUser(const data::User &user);

    // Отправка файла в текущий чат (используется кнопкой «скрепка» и тестами).
    void sendFile(const QString &filePath);

    // Открыть чат по id (из уведомления или поиска).
    void openChatById(qint64 chatId);
    qint64 currentChatId() const { return m_currentChatId; }

    // Показать окно поверх остальных.
    void activate();

    // Открыть глобальный поиск (Ctrl+F).
    void openSearch();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onChatsChanged();
    void onMessagesChanged(qint64 chatId);
    void onChatSelectionChanged();
    void onSendRequested(const QString &text);
    void onThemeToggleClicked();
    void onNewChatClicked();
    void onTypingChanged(qint64 chatId, qint64 userId, bool typing);
    void onPresenceChanged(qint64 userId, bool online);
    void onError(const QString &message);
    void openSettings();

private:
    void buildUi();
    QWidget *buildSidebar();
    QWidget *buildChatPane();
    QWidget *buildEmptyPane();
    QWidget *buildConversationPane();

    void openChat(qint64 chatId);
    void selectChatRow(qint64 chatId);
    void updateHeader();
    void updateThemeButton();
    void updateConnectionBanner();
    void restoreWindowState();
    QHash<qint64, QString> chatNames() const;

    app::ChatService *m_chats = nullptr;
    app::SessionService *m_session = nullptr;
    app::SettingsStore *m_settings = nullptr;
    app::ThemeManager *m_theme = nullptr;
    data::User m_user;

    AvatarWidget *m_profileAvatar = nullptr;
    QLabel *m_profileName = nullptr;
    QLabel *m_profileSub = nullptr;
    QToolButton *m_themeButton = nullptr;
    QLineEdit *m_searchField = nullptr;
    QListView *m_chatList = nullptr;
    ChatListModel *m_chatModel = nullptr;
    QSortFilterProxyModel *m_chatProxy = nullptr;

    QStackedWidget *m_chatStack = nullptr;
    AvatarWidget *m_headerAvatar = nullptr;
    QLabel *m_headerTitle = nullptr;
    QLabel *m_headerStatus = nullptr;
    MessageView *m_messageView = nullptr;
    ComposerWidget *m_composer = nullptr;
    QLabel *m_connectionBanner = nullptr;
    QTimer *m_typingTimer = nullptr;
    QPointer<SearchDialog> m_searchDialog;
    qint64 m_pendingScrollMessageId = 0;

    qint64 m_currentChatId = 0;
    QHash<qint64, bool> m_online;
    qint64 m_typingUserId = 0;
};

} // namespace ui
