#pragma once

#include "data/Database.h"
#include "data/Models.h"
#include "server/AttachmentStore.h"
#include "server/HandlerResult.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace server {

class ClientSession;

// Ядро сервера: аутентификация, чаты, сообщения, квитанции и рассылка событий.
// Работает в потоке event-loop сервера; запросы к SQLite короткие (WAL).
class ServerCore : public QObject
{
    Q_OBJECT

public:
    explicit ServerCore(QObject *parent = nullptr);
    ~ServerCore() override;

    bool initialize(const QString &databasePath, const QString &attachmentsRoot, QString *error);

    HandlerResult handle(const QString &type, const QJsonObject &data, ClientSession *session);

    void registerSession(ClientSession *session);
    void unregisterSession(ClientSession *session);

    data::Database &database() { return m_database; }
    QString attachmentsRoot() const { return m_attachmentsRoot; }
    int onlineUsers() const { return m_sessions.size(); }

private:
    HandlerResult handleHello(const QJsonObject &data);
    HandlerResult handleRegister(const QJsonObject &data, ClientSession *session);
    HandlerResult handleLogin(const QJsonObject &data, ClientSession *session);
    HandlerResult handleResume(const QJsonObject &data, ClientSession *session);
    HandlerResult handleLogout(ClientSession *session);

    HandlerResult handleChatList(ClientSession *session);
    HandlerResult handleChatOpen(const QJsonObject &data, ClientSession *session);
    HandlerResult handleChatDirect(const QJsonObject &data, ClientSession *session);
    HandlerResult handleChatRead(const QJsonObject &data, ClientSession *session);
    HandlerResult handleChatCreate(const QJsonObject &data, ClientSession *session);
    HandlerResult handleChatMembers(const QJsonObject &data, ClientSession *session);
    HandlerResult handleChatAdd(const QJsonObject &data, ClientSession *session);
    HandlerResult handleChatLeave(const QJsonObject &data, ClientSession *session);
    HandlerResult handleUserSearch(const QJsonObject &data, ClientSession *session);

    HandlerResult handleMessageSend(const QJsonObject &data, ClientSession *session);
    HandlerResult handleMessageEdit(const QJsonObject &data, ClientSession *session);
    HandlerResult handleMessageDelete(const QJsonObject &data, ClientSession *session);
    HandlerResult handleReceiptDelivered(const QJsonObject &data, ClientSession *session);
    HandlerResult handleTyping(const QJsonObject &data, ClientSession *session);

    HandlerResult handleAttachBegin(const QJsonObject &data, ClientSession *session);
    HandlerResult handleAttachFinish(const QJsonObject &data, ClientSession *session);
    HandlerResult handleAttachGet(const QJsonObject &data, ClientSession *session);

public:
    // Бинарный кадр от клиента (чанк загрузки).
    bool handleChunk(ClientSession *session, quint32 transferId, quint32 sequence,
                     const QByteArray &data, QString *error);

private:

    QString issueToken(qint64 userId, QString *error);
    bool resolveToken(const QString &token, qint64 *userId, QString *error);
    void dropToken(const QString &token);

    void broadcastToChat(qint64 chatId, const QString &event, const QJsonObject &data,
                         ClientSession *skip = nullptr);
    void sendToUser(qint64 userId, const QString &event, const QJsonObject &data,
                    ClientSession *skip = nullptr);
    void deliver(qint64 userId, const QJsonObject &object, ClientSession *skip);
    void broadcastPresence(qint64 userId, bool online);

    data::Database m_database;
    QString m_attachmentsRoot;
    AttachmentStore m_attachmentStore;
    QHash<quint32, ReadyAttachment> m_readyAttachments;
    QMultiHash<qint64, ClientSession *> m_sessions;

    bool isUserOnline(qint64 userId) const;
};

} // namespace server
