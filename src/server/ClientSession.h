#pragma once

#include "core/FrameCodec.h"
#include "core/Protocol.h"

#include <QHash>
#include <QObject>
#include <QString>

class QFile;
class QJsonObject;
class QTcpSocket;

namespace server {

class ServerCore;

// Одно клиентское соединение: разбор кадров, авторизация, отправка ответов и событий.
class ClientSession : public QObject
{
    Q_OBJECT

public:
    ClientSession(QTcpSocket *socket, ServerCore *core, QObject *parent = nullptr);
    ~ClientSession() override;

    qint64 userId() const { return m_userId; }
    QString username() const { return m_username; }
    QString token() const { return m_token; }
    bool isAuthenticated() const { return m_userId > 0; }

    void setAuthenticated(qint64 userId, const QString &username);
    void setToken(const QString &token) { m_token = token; }

    void sendJson(const QJsonObject &object);
    void sendEvent(const QString &event, const QJsonObject &data);

    // Голосовой кадр (Opus) клиенту от имени участника userId.
    void sendVoiceFrame(quint32 chatId, quint32 userId, quint32 sequence,
                        const QByteArray &data);

    // Выдача файла клиенту: сервер сам режет файл на чанки и шлёт attach.end.
    quint32 allocateDownloadId();
    void queueDownload(quint32 transferId, const QString &path, qint64 size, qint64 offset = 0);
    void startPendingDownloads();

    QString peerAddress() const;

    // Ограничение частоты запросов: token bucket (30 запросов/с, burst 60).
    bool allowRequest();
    void setRateLimit(double perSecond, double capacity);

private slots:
    void onReadyRead();
    void onDisconnected();
    void onBytesWritten(qint64 bytes);

private:
    struct Download {
        quint32 id = 0;
        QString path;
        qint64 size = 0;
        qint64 sent = 0;
        qint64 offset = 0;
        quint32 sequence = 0;
        QFile *file = nullptr;
        bool started = false;
    };

    void handleFrame(const core::Frame &frame);
    void handleRequest(const QJsonObject &request);
    void pumpDownloads();
    void closeDownloads();

    QTcpSocket *m_socket = nullptr;
    ServerCore *m_core = nullptr;
    core::FrameCodec m_codec;
    qint64 m_userId = 0;
    QString m_username;
    QString m_token;
    QHash<quint32, Download> m_downloads;
    quint32 m_nextDownloadId = 1;
    bool m_pumping = false;
    bool m_closing = false;

    // Token bucket для ограничения частоты запросов.
    double m_ratePerSecond = 60.0;
    double m_rateCapacity = 300.0;
    double m_tokens = 300.0;
    qint64 m_tokensUpdatedAt = 0;
};

} // namespace server
