#pragma once

#include "app/ChatService.h"
#include "data/Models.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>

class QLabel;
class QLineEdit;
class QListView;
class QPoint;
class QSortFilterProxyModel;
class QStackedWidget;
class QTimer;
class QToolButton;

namespace app {
class BillingService;
class SessionService;
class SettingsStore;
class ThemeManager;
class UpdateService;
class VoiceService;
} // namespace app

namespace net {
class ClientConnection;
} // namespace net

namespace ui {

class AvatarWidget;
class ChatListModel;
class ComposerWidget;
class MessageView;
class ParticipantsPanel;
class ProfileCardDialog;
class SearchDialog;

// Главное окно: список чатов и переписка.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(app::ChatService *chats, app::SessionService *session,
               net::ClientConnection *connection, app::SettingsStore *settings,
               app::ThemeManager *theme, app::BillingService *billing = nullptr,
               app::VoiceService *voice = nullptr, QWidget *parent = nullptr);
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
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onChatsChanged();
    void onMessagesChanged(qint64 chatId);
    void onChatSelectionChanged();
    void onChatListMenu(const QPoint &position);
    void onEditMessage(qint64 messageId);
    void onDeleteMessageForEveryone(qint64 messageId);
    void onDeleteMessageForMe(qint64 messageId);
    void onMembersClicked();
    void onCreateGroup();
    void onVoiceClicked();
    void onMuteClicked();
    void updateVoiceUi();
    void onIncomingCall(qint64 chatId, qint64 callerId, const QString &callerName);
    void onCallAccepted(qint64 chatId, qint64 userId);
    void onCallDeclined(qint64 chatId, qint64 userId);
    void onCallCancelled(qint64 chatId);
    void openProfile(qint64 userId);
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
    void showToast(const QString &text);
    QHash<qint64, QString> chatNames() const;

    app::ChatService *m_chats = nullptr;
    app::SessionService *m_session = nullptr;
    app::SettingsStore *m_settings = nullptr;
    app::ThemeManager *m_theme = nullptr;
    app::BillingService *m_billing = nullptr;
    app::VoiceService *m_voice = nullptr;
    app::UpdateService *m_updater = nullptr;
    net::ClientConnection *m_connection = nullptr;
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
    QToolButton *m_membersButton = nullptr;
    QToolButton *m_voiceButton = nullptr;
    QToolButton *m_muteButton = nullptr;
    ParticipantsPanel *m_participantsPanel = nullptr;
    bool m_panelVisible = false;
    qint64 m_speakingUserId = 0;
    int m_groupMemberCount = 0;
    MessageView *m_messageView = nullptr;
    ComposerWidget *m_composer = nullptr;
    QLabel *m_connectionBanner = nullptr;
    QLabel *m_toast = nullptr;
    QTimer *m_typingTimer = nullptr;
    QTimer *m_toastTimer = nullptr;
    QPointer<SearchDialog> m_searchDialog;
    qint64 m_pendingScrollMessageId = 0;

    qint64 m_currentChatId = 0;
    QHash<qint64, bool> m_online;
    qint64 m_typingUserId = 0;
};

} // namespace ui
