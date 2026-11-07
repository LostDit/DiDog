#pragma once

#include "core/FrameCodec.h"

#include <QAbstractSocket>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

class QTcpSocket;
class QTimer;

namespace net {

class RequestTracker;

// Клиентское TCP-соединение: фрейминг, запросы с callback, авто-переподключение.
// Сокет живёт в GUI-потоке и работает асинхронно — отдельный поток не нужен.
class ClientConnection : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        Connecting,
        Connected
    };
    Q_ENUM(State)

    using Callback = std::function<void(const QJsonObject &reply)>;

    explicit ClientConnection(QObject *parent = nullptr);
    ~ClientConnection() override;

    void setEndpoint(const QString &host, quint16 port);
    QString host() const { return m_host; }
    quint16 port() const { return m_port; }

    State state() const { return m_state; }
    bool isConnected() const;

    void connectToServer();
    void disconnectFromServer();

    void setAutoReconnect(bool enabled) { m_autoReconnect = enabled; }
    bool autoReconnect() const { return m_autoReconnect; }

    // Запрос: callback вызывается ровно один раз (ответ, таймаут или обрыв).
    QString request(const QString &type, const QJsonObject &data, Callback callback,
                    int timeoutMs = 30000);

    // Отправка бинарного чанка (загрузка вложения).
    bool sendChunk(quint32 transferId, quint32 sequence, const QByteArray &data);

    qint64 bytesToWrite() const;

signals:
    void stateChanged(net::ClientConnection::State state);
    void socketConnected();
    void socketDisconnected();
    void eventReceived(const QString &event, const QJsonObject &data);
    void errorOccurred(const QString &message);
    void reconnectScheduled(int delayMs);

    // Бинарные кадры и завершение выдачи файла (attach.end).
    void chunkReceived(quint32 transferId, quint32 sequence, const QByteArray &data);
    void transferFinished(quint32 transferId);
    void bytesWritten(qint64 bytes);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    void setState(State state);
    bool sendObject(const QJsonObject &object);
    void handleFrame(const core::Frame &frame);
    void scheduleReconnect();
    void cancelReconnect();

    QTcpSocket *m_socket = nullptr;
    RequestTracker *m_tracker = nullptr;
    core::FrameCodec m_codec;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_heartbeatTimer = nullptr;

    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = 45000;
    State m_state = State::Disconnected;
    bool m_autoReconnect = true;
    bool m_userDisconnect = false;
    int m_reconnectAttempt = 0;
};

} // namespace net

Q_DECLARE_METATYPE(net::ClientConnection::State)
