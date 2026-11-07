#pragma once

#include "data/Models.h"
#include "net/ClientConnection.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

namespace app {

class AttachmentService;
class HistoryStore;

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
    void sendText(qint64 chatId, const QString &text);
    void sendFile(qint64 chatId, const QString &filePath);
    void markRead(qint64 chatId);
    void createDirect(const QString &username);
    void retryFailed(qint64 chatId, const QString &clientMsgId);
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
    void hydrateLocalPaths(qint64 chatId);

    net::ClientConnection *m_connection = nullptr;
    HistoryStore *m_history = nullptr;
    AttachmentService *m_attachments = nullptr;
    qint64 m_currentUserId = 0;
    data::User m_currentUser;

    QList<data::ChatSummary> m_chats;
    QHash<qint64, ChatState> m_states;
    qint64 m_activeChatId = 0;
};

} // namespace app
