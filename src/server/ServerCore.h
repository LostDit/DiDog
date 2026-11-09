#pragma once

#include "data/Database.h"
#include "data/Models.h"
#include "server/AttachmentStore.h"
#include "server/HandlerResult.h"

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

class QUdpSocket;

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

    // Каталог обновлений (manifest.json + файлы). По умолчанию — рядом с attachments.
    void setUpdatesRoot(const QString &root) { m_updatesRoot = root; }
    QString updatesRoot() const { return m_updatesRoot; }

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
    HandlerResult handleBillingStatus(ClientSession *session);
    HandlerResult handleBillingRedeem(const QJsonObject &data, ClientSession *session);
    HandlerResult handleThemeList(ClientSession *session);
    HandlerResult handleThemeGet(const QJsonObject &data, ClientSession *session);
    HandlerResult handleVoiceJoin(const QJsonObject &data, ClientSession *session);
    HandlerResult handleVoiceLeave(const QJsonObject &data, ClientSession *session);
    HandlerResult handleVoiceInvite(const QJsonObject &data, ClientSession *session);
    HandlerResult handleVoiceAccept(const QJsonObject &data, ClientSession *session);
    HandlerResult handleVoiceDecline(const QJsonObject &data, ClientSession *session);
    HandlerResult handleVoiceCancel(const QJsonObject &data, ClientSession *session);
    HandlerResult handleVoiceSpeaking(const QJsonObject &data, ClientSession *session);
    HandlerResult handleUpdateManifest(ClientSession *session);
    HandlerResult handleUpdateDownload(const QJsonObject &data, ClientSession *session);

    void leaveVoiceRoom(ClientSession *session);

    HandlerResult handleMessageSend(const QJsonObject &data, ClientSession *session);
    HandlerResult handleMessageEdit(const QJsonObject &data, ClientSession *session);
    HandlerResult handleMessageDelete(const QJsonObject &data, ClientSession *session);
    HandlerResult handleMessageReact(const QJsonObject &data, ClientSession *session);
    HandlerResult handleReceiptDelivered(const QJsonObject &data, ClientSession *session);
    HandlerResult handleTyping(const QJsonObject &data, ClientSession *session);

    HandlerResult handleAttachBegin(const QJsonObject &data, ClientSession *session);
    HandlerResult handleAttachFinish(const QJsonObject &data, ClientSession *session);
    HandlerResult handleAttachGet(const QJsonObject &data, ClientSession *session);

public:
    // Бинарный кадр от клиента (чанк загрузки).
    bool handleChunk(ClientSession *session, quint32 transferId, quint32 sequence,
                     const QByteArray &data, QString *error);

    // Голосовой кадр: проверяет участие в голосовой комнате и пересылает остальным.
    void handleVoiceFrame(ClientSession *session, qint64 chatId, quint32 sequence,
                          const QByteArray &data);

    // UDP-релей голоса: сокет, токены комнат и пересылка дейтаграмм.
    bool startVoiceSocket(quint16 port, QString *error);
    void stopVoiceSocket();
    quint16 voicePort() const { return m_voicePort; }

    // Создание промокода (CLI администратора).
    bool createPromo(const QString &code, const QString &plan, int days, int maxUses,
                     QString *error);

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

    // Каталог обновлений: manifest.json + файлы новой версии.
    struct UpdateFile {
        QString path;
        qint64 size = 0;
        QString sha256;
    };
    QString m_updatesRoot;
    bool loadUpdateManifest(QString *version, QList<UpdateFile> *files, QString *error) const;
    void attachReactions(QList<data::Message> *messages, qint64 viewerId) const;
    AttachmentStore m_attachmentStore;
    QHash<quint32, ReadyAttachment> m_readyAttachments;
    QMultiHash<qint64, ClientSession *> m_sessions;
    QHash<qint64, QSet<ClientSession *>> m_voiceRooms;

    struct VoiceEndpoint {
        qint64 chatId = 0;
        qint64 userId = 0;
        QHostAddress address;
        quint16 port = 0;
        quint32 lastSequence = 0;
    };

    void onVoiceDatagram();
    void removeVoiceEndpoint(ClientSession *session);

    QUdpSocket *m_voiceSocket = nullptr;
    quint16 m_voicePort = 45001;
    QHash<quint32, VoiceEndpoint> m_voiceEndpoints;
    QHash<ClientSession *, quint32> m_sessionVoiceTokens;
    QHash<qint64, QByteArray> m_voiceKeys;

    bool isUserOnline(qint64 userId) const;
};

} // namespace server
