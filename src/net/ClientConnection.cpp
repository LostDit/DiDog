#include "net/ClientConnection.h"

#include "core/Ids.h"
#include "core/Limits.h"
#include "core/Protocol.h"
#include "net/RequestTracker.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSslCertificate>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>

namespace net {
namespace {

constexpr int kReconnectBaseMs = 1000;
constexpr int kReconnectMaxMs = 30000;

QString fingerprintOf(const QSslCertificate &certificate)
{
    if (certificate.isNull())
        return QString();

    return QString::fromLatin1(certificate.digest(QCryptographicHash::Sha256).toHex());
}

} // namespace

ClientConnection::ClientConnection(QObject *parent)
    : QObject(parent)
    , m_tracker(new RequestTracker(this))
    , m_reconnectTimer(new QTimer(this))
    , m_heartbeatTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);

    rebuildSocket();

    connect(m_reconnectTimer, &QTimer::timeout, this, &ClientConnection::connectToServer);

    m_heartbeatTimer->setInterval(core::limits::kPingIntervalMs);
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        if (m_state == State::Connected)
            request(QStringLiteral("ping"), QJsonObject(), nullptr, 10000);
    });
}

ClientConnection::~ClientConnection() = default;

void ClientConnection::rebuildSocket()
{
    QTcpSocket *old = m_socket;

    m_socket = m_tlsEnabled ? static_cast<QTcpSocket *>(new QSslSocket(this))
                            : new QTcpSocket(this);

    if (old) {
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }

    wireSocket();
}

void ClientConnection::wireSocket()
{
    connect(m_socket, &QTcpSocket::connected, this, &ClientConnection::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientConnection::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &ClientConnection::onReadyRead);
    // В Qt 5.15 сигнал error() объявлен устаревшим; на 5.14 используем его.
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, &ClientConnection::onSocketError);
#else
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &ClientConnection::onSocketError);
#endif
    connect(m_socket, &QTcpSocket::bytesWritten, this, &ClientConnection::bytesWritten);

    if (auto *ssl = qobject_cast<QSslSocket *>(m_socket)) {
        connect(ssl, &QSslSocket::encrypted, this, &ClientConnection::onEncrypted);
        connect(ssl, QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),
                this, &ClientConnection::onSslErrors);
    }
}

void ClientConnection::setTlsEnabled(bool enabled)
{
    if (m_tlsEnabled == enabled || m_state != State::Disconnected)
        return;

    m_tlsEnabled = enabled;
    rebuildSocket();
}

void ClientConnection::setEndpoint(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
}

bool ClientConnection::isConnected() const
{
    return m_state == State::Connected;
}

void ClientConnection::connectToServer()
{
    if (m_state != State::Disconnected)
        return;
    if (m_host.isEmpty() || m_port == 0)
        return;

    m_userDisconnect = false;
    setState(State::Connecting);

    if (m_tlsEnabled) {
        auto *ssl = qobject_cast<QSslSocket *>(m_socket);
        if (!ssl) {
            setState(State::Disconnected);
            return;
        }
        ssl->connectToHostEncrypted(m_host, m_port);
        return;
    }

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
    // Для TLS соединение готово только после handshake.
    if (m_tlsEnabled)
        return;

    finishConnect();
}

void ClientConnection::onEncrypted()
{
    auto *ssl = qobject_cast<QSslSocket *>(m_socket);
    if (!ssl)
        return;

    const QString fingerprint = fingerprintOf(ssl->peerCertificate());
    m_peerPin = fingerprint;

    if (!fingerprint.isEmpty() && m_expectedPin.isEmpty()) {
        // Первое подключение: запоминаем отпечаток (TOFU).
        emit certificatePinned(fingerprint);
    }

    finishConnect();
}

void ClientConnection::finishConnect()
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

void ClientConnection::onSslErrors(const QList<QSslError> &errors)
{
    Q_UNUSED(errors)

    auto *ssl = qobject_cast<QSslSocket *>(m_socket);
    if (!ssl)
        return;

    const QString fingerprint = fingerprintOf(ssl->peerCertificate());

    if (fingerprint.isEmpty()) {
        emit errorOccurred(tr("Сервер не предъявил сертификат."));
        m_userDisconnect = true;
        ssl->abort();
        setState(State::Disconnected);
        return;
    }

    if (!m_expectedPin.isEmpty() && m_expectedPin.compare(fingerprint, Qt::CaseInsensitive) != 0) {
        emit errorOccurred(tr("Сертификат сервера не совпадает с сохранённым. "
                              "Возможна подмена сервера."));
        m_userDisconnect = true;
        ssl->abort();
        setState(State::Disconnected);
        return;
    }

    // Сертификат самоподписанный: подлинность проверяется по отпечатку выше.
    ssl->ignoreSslErrors();
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
    if (frame.type == core::FrameType::Binary) {
        quint32 transferId = 0;
        quint32 sequence = 0;
        QByteArray data;
        if (core::decodeChunk(frame.payload, &transferId, &sequence, &data))
            emit chunkReceived(transferId, sequence, data);
        return;
    }

    if (frame.type == core::FrameType::Voice) {
        quint32 chatId = 0;
        quint32 userId = 0;
        quint32 sequence = 0;
        QByteArray data;
        if (core::proto::decodeVoice(frame.payload, &chatId, &userId, &sequence, &data))
            emit voiceFrameReceived(chatId, userId, sequence, data);
        return;
    }

    QJsonObject object;
    QString error;
    if (!core::proto::decode(frame.payload, &object, &error)) {
        qWarning().noquote() << QStringLiteral("Некорректный JSON от сервера: %1").arg(error);
        return;
    }

    const QString type = object.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("attach.end")) {
        const quint32 transferId = static_cast<quint32>(
            object.value(QStringLiteral("data")).toObject()
                .value(QStringLiteral("transfer_id")).toDouble());
        emit transferFinished(transferId);
        return;
    }

    if (type == QLatin1String("ev")) {
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
    if (m_state != State::Connected)
        return false;

    const QByteArray payload = core::proto::encode(object);
    const QByteArray frame = core::FrameCodec::encode(core::FrameType::Json, payload);

    return m_socket->write(frame) == frame.size();
}

bool ClientConnection::sendChunk(quint32 transferId, quint32 sequence, const QByteArray &data)
{
    if (m_state != State::Connected)
        return false;

    const QByteArray chunk = core::encodeChunk(transferId, sequence, data);
    const QByteArray frame = core::FrameCodec::encode(core::FrameType::Binary, chunk);
    return m_socket->write(frame) == frame.size();
}

qint64 ClientConnection::bytesToWrite() const
{
    return m_socket->bytesToWrite();
}

bool ClientConnection::sendVoiceFrame(quint32 chatId, quint32 sequence, const QByteArray &data)
{
    if (m_state != State::Connected)
        return false;

    const QByteArray payload = core::proto::encodeVoice(chatId, 0, sequence, data);
    const QByteArray frame = core::FrameCodec::encode(core::FrameType::Voice, payload);
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
