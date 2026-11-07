#pragma once

#include "core/FrameCodec.h"
#include "core/Protocol.h"

#include <QObject>
#include <QPointer>
#include <QString>

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

    QString peerAddress() const;

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    void handleFrame(const core::Frame &frame);
    void handleRequest(const QJsonObject &request);

    QTcpSocket *m_socket = nullptr;
    QPointer<ServerCore> m_core;
    core::FrameCodec m_codec;
    qint64 m_userId = 0;
    QString m_username;
    QString m_token;
    bool m_closing = false;
};

} // namespace server
