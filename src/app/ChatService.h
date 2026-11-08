#pragma once

#include "data/Models.h"
#include "net/ClientConnection.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

namespace app {

class AttachmentService;
class HistoryStore;
class SettingsStore;

// Состояние переписки на клиенте: список чатов, ленты сообщений, отправка.
// Живёт в GUI-потоке; сетевые запросы асинхронны, история — в рабочем потоке.
class ChatService : public QObject
{
    Q_OBJECT

public:
    ChatService(net::ClientConnection *connection, HistoryStore *history,
                AttachmentService *attachments, QObject *parent = nullptr);

    void setCurrentUser(const data::User &user);
    qint64 currentUserId() const { return m_currentUserId; }

    void setSettingsStore(SettingsStore *settings);

    // Удаление чата у себя: скрывает его и стирает локальную историю.
    void deleteChat(qint64 chatId);
    // Очистка локальной истории, чат остаётся в списке.
    void clearHistory(qint64 chatId);

    // Редактирование и удаление сообщений.
    void editMessage(qint64 chatId, qint64 messageId, const QString &body);
    void deleteMessageForEveryone(qint64 chatId, qint64 messageId);
    void deleteMessageForMe(qint64 chatId, qint64 messageId);

    QList<data::ChatSummary> chats() const { return m_chats; }
    data::ChatSummary chat(qint64 chatId) const;
    QList<data::Message> messages(qint64 chatId) const;
    bool hasMore(qint64 chatId) const;
    bool isLoaded(qint64 chatId) const;
    int indexOfChat(qint64 chatId) const;

    void setActiveChat(qint64 chatId) { m_activeChatId = chatId; }

    void loadChats();
    void openChat(qint64 chatId);
    void loadOlder(qint64 chatId);
    void sendText(qint64 chatId, const QString &text, qint64 replyToId = 0);
    void sendFile(qint64 chatId, const QString &filePath);
    void markRead(qint64 chatId);
    void createDirect(const QString &username);

    // Групповые чаты.
    void createGroup(const QString &title, const QList<qint64> &memberIds);
    void loadMembers(qint64 chatId);
    void addMembers(qint64 chatId, const QList<qint64> &userIds);
    void leaveChat(qint64 chatId);
    void searchUsers(const QString &query);
    void retryFailed(qint64 chatId, const QString &clientMsgId);

    // Повторное скачивание вложения (клик по «Не удалось загрузить»).
    void retryAttachment(qint64 attachmentId);
    void searchMessages(const QString &query);
    int totalUnread() const;

    // Отправляет накопленные офлайн-сообщения (вызывается после переподключения).
    void flushOutbox();

signals:
    void chatsChanged();
    void messagesChanged(qint64 chatId);
    void historyPrepend(qint64 chatId);
    void chatAdded(qint64 chatId);
    void errorOccurred(const QString &message);
    void typingChanged(qint64 chatId, qint64 userId, bool typing);
    void presenceChanged(qint64 userId, bool online);
    void messageArrived(qint64 chatId, const data::Message &message);
    void searchResults(const QString &query, const QList<data::Message> &messages);
    void groupCreated(qint64 chatId);
    void membersLoaded(qint64 chatId, const QList<data::ChatMember> &members);
    void membersChanged(qint64 chatId);
    void usersFound(const QString &query, const QList<data::User> &users);
    void leftChat(qint64 chatId);

private slots:
    void onEvent(const QString &event, const QJsonObject &data);

private:
    struct ChatState {
        QList<data::Message> messages;
        bool loaded = false;
        bool hasMore = false;
        bool loading = false;
    };

    void handleNewMessage(const data::Message &message);
    void applyReceipt(qint64 chatId, qint64 messageId, data::DeliveryState state);
    void applyRead(qint64 chatId, qint64 upToSeq);
    void upsertSummary(const data::ChatSummary &summary);
    void insertMessageSorted(qint64 chatId, const data::Message &message);
    void updateSummaryForMessage(qint64 chatId, const data::Message &message);
    void sendPending(const data::Message &message);
    void persistChats();
    void persistOutbox(qint64 chatId, const QString &clientMsgId);
    void resolveReplyPreview(qint64 chatId, data::Message *message) const;
    void hydrateAttachments(qint64 chatId);
    void unhideChat(qint64 chatId);
    void saveHiddenChats();
    void saveHiddenMessages();
    bool isMessageHidden(qint64 messageId) const;
    void applyMessageUpdate(const data::Message &message);

    net::ClientConnection *m_connection = nullptr;
    HistoryStore *m_history = nullptr;
    AttachmentService *m_attachments = nullptr;
    SettingsStore *m_settings = nullptr;
    qint64 m_currentUserId = 0;
    data::User m_currentUser;

    QList<data::ChatSummary> m_chats;
    QHash<qint64, ChatState> m_states;
    QHash<qint64, int> m_downloadAttempts;
    QSet<qint64> m_hiddenChats;
    QSet<qint64> m_hiddenMessages;
    qint64 m_activeChatId = 0;
};

} // namespace app
