#include "net/ClientConnection.h"

#include "core/Ids.h"
#include "core/Limits.h"
#include "core/Protocol.h"
#include "net/RequestTracker.h"

#include <QDebug>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>

namespace net {
namespace {

constexpr int kReconnectBaseMs = 1000;
constexpr int kReconnectMaxMs = 30000;

} // namespace

ClientConnection::ClientConnection(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_tracker(new RequestTracker(this))
    , m_reconnectTimer(new QTimer(this))
    , m_heartbeatTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);

    connect(m_socket, &QTcpSocket::connected, this, &ClientConnection::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientConnection::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &ClientConnection::onReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &ClientConnection::onSocketError);

    connect(m_reconnectTimer, &QTimer::timeout, this, &ClientConnection::connectToServer);

    m_heartbeatTimer->setInterval(core::limits::kPingIntervalMs);
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        if (m_state == State::Connected)
            request(QStringLiteral("ping"), QJsonObject(), nullptr, 10000);
    });
}

ClientConnection::~ClientConnection() = default;

void ClientConnection::setEndpoint(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
}

bool ClientConnection::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void ClientConnection::connectToServer()
{
    if (m_state != State::Disconnected)
        return;
    if (m_host.isEmpty() || m_port == 0)
        return;

    m_userDisconnect = false;
    setState(State::Connecting);
    m_socket->connectToHost(m_host, m_port);
}

void ClientConnection::disconnectFromServer()
{
    m_userDisconnect = true;
    cancelReconnect();
    m_tracker->failAll(core::ErrorCode::Offline);
    m_heartbeatTimer->stop();

    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();

    setState(State::Disconnected);
}

QString ClientConnection::request(const QString &type, const QJsonObject &data, Callback callback,
                                  int timeoutMs)
{
    const QString requestId = core::newUuid();
    const QJsonObject request = core::proto::request(type, data, requestId);

    if (m_state != State::Connected) {
        const QJsonObject error = core::proto::errorReply(requestId, core::ErrorCode::Offline);
        if (callback) {
            QTimer::singleShot(0, this, [callback, error]() { callback(error); });
        }
        return requestId;
    }

    if (callback)
        m_tracker->track(requestId, callback, timeoutMs);

    if (!sendObject(request)) {
        m_tracker->complete(requestId,
                            core::proto::errorReply(requestId, core::ErrorCode::Offline));
    }
    return requestId;
}

void ClientConnection::onConnected()
{
    m_reconnectAttempt = 0;
    m_userDisconnect = false;
    setState(State::Connected);
    m_heartbeatTimer->start();
    emit socketConnected();
}

void ClientConnection::onDisconnected()
{
    m_heartbeatTimer->stop();
    m_tracker->failAll(core::ErrorCode::Offline);
    setState(State::Disconnected);
    emit socketDisconnected();

    if (m_autoReconnect && !m_userDisconnect)
        scheduleReconnect();
}

void ClientConnection::onReadyRead()
{
    m_codec.append(m_socket->readAll());

    core::Frame frame;
    QString error;
    for (;;) {
        const core::FrameCodec::Status status = m_codec.next(&frame, &error);
        if (status == core::FrameCodec::Status::NeedMore)
            return;

        if (status == core::FrameCodec::Status::Error) {
            qWarning().noquote() << QStringLiteral("Нарушение протокола: %1").arg(error);
            emit errorOccurred(error);
            m_socket->disconnectFromHost();
            return;
        }

        handleFrame(frame);
    }
}

void ClientConnection::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)

    const QString message = m_socket->errorString();
    emit errorOccurred(message);

    // Отказ в соединении не всегда сопровождается signal disconnected():
    // если подключение так и не установилось, планируем повтор сами.
    if (m_state == State::Connecting) {
        setState(State::Disconnected);
        if (m_autoReconnect && !m_userDisconnect)
            scheduleReconnect();
    }
}

void ClientConnection::handleFrame(const core::Frame &frame)
{
    if (frame.type != core::FrameType::Json) {
        qWarning() << "Неожиданный бинарный кадр от сервера";
        return;
    }

    QJsonObject object;
    QString error;
    if (!core::proto::decode(frame.payload, &object, &error)) {
        qWarning().noquote() << QStringLiteral("Некорректный JSON от сервера: %1").arg(error);
        return;
    }

    if (object.value(QStringLiteral("type")).toString() == QLatin1String("ev")) {
        const QString event = object.value(QStringLiteral("event")).toString();
        emit eventReceived(event, object.value(QStringLiteral("data")).toObject());
        return;
    }

    const QString replyTo = object.value(QStringLiteral("reply_to")).toString();
    if (!replyTo.isEmpty())
        m_tracker->complete(replyTo, object);
}

void ClientConnection::setState(State state)
{
    if (m_state == state)
        return;

    m_state = state;
    emit stateChanged(state);
}

bool ClientConnection::sendObject(const QJsonObject &object)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState)
        return false;

    const QByteArray payload = core::proto::encode(object);
    const QByteArray frame = core::FrameCodec::encode(core::FrameType::Json, payload);

    return m_socket->write(frame) == frame.size();
}

void ClientConnection::scheduleReconnect()
{
    if (m_reconnectTimer->isActive())
        return;

    const int exponent = qMin(m_reconnectAttempt, 5);
    const int base = qMin(kReconnectBaseMs * (1 << exponent), kReconnectMaxMs);
    const int jitter = QRandomGenerator::global()->bounded(base / 5 + 1);
    const int delay = base + jitter;

    ++m_reconnectAttempt;
    emit reconnectScheduled(delay);
    m_reconnectTimer->start(delay);
}

void ClientConnection::cancelReconnect()
{
    m_reconnectTimer->stop();
    m_reconnectAttempt = 0;
}

} // namespace net
