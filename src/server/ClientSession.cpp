#include "server/ClientSession.h"

#include "server/ServerCore.h"

#include <QDebug>
#include <QHostAddress>
#include <QJsonObject>
#include <QTcpSocket>

namespace server {

ClientSession::ClientSession(QTcpSocket *socket, ServerCore *core, QObject *parent)
    : QObject(parent)
    , m_socket(socket)
    , m_core(core)
{
    m_socket->setParent(this);
    connect(m_socket, &QTcpSocket::readyRead, this, &ClientSession::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientSession::onDisconnected);
}

ClientSession::~ClientSession()
{
    // Ядро может быть уничтожено раньше сессии (порядок удаления детей QObject),
    // поэтому указатель защищён QPointer.
    if (m_core && isAuthenticated())
        m_core->unregisterSession(this);
}

QString ClientSession::peerAddress() const
{
    if (!m_socket)
        return QString();
    return QStringLiteral("%1:%2")
        .arg(m_socket->peerAddress().toString())
        .arg(m_socket->peerPort());
}

void ClientSession::setAuthenticated(qint64 userId, const QString &username)
{
    m_userId = userId;
    m_username = username;
}

void ClientSession::sendJson(const QJsonObject &object)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    const QByteArray payload = core::proto::encode(object);
    const QByteArray frame = core::FrameCodec::encode(core::FrameType::Json, payload);

    if (m_socket->write(frame) == -1) {
        qWarning().noquote() << QStringLiteral("Ошибка записи %1: %2")
                                    .arg(peerAddress(), m_socket->errorString());
        m_socket->disconnectFromHost();
    }
}

void ClientSession::sendEvent(const QString &event, const QJsonObject &data)
{
    sendJson(core::proto::eventObject(event, data));
}

void ClientSession::onReadyRead()
{
    m_codec.append(m_socket->readAll());

    core::Frame frame;
    QString error;
    for (;;) {
        const core::FrameCodec::Status status = m_codec.next(&frame, &error);
        if (status == core::FrameCodec::Status::NeedMore)
            return;

        if (status == core::FrameCodec::Status::Error) {
            qWarning().noquote() << QStringLiteral("Протокол нарушен (%1): %2")
                                        .arg(peerAddress(), error);
            m_socket->disconnectFromHost();
            return;
        }

        handleFrame(frame);
    }
}

void ClientSession::handleFrame(const core::Frame &frame)
{
    if (frame.type != core::FrameType::Json) {
        qWarning().noquote() << QStringLiteral("Неожиданный бинарный кадр от %1")
                                    .arg(peerAddress());
        m_socket->disconnectFromHost();
        return;
    }

    QJsonObject request;
    QString error;
    if (!core::proto::decode(frame.payload, &request, &error)) {
        qWarning().noquote() << QStringLiteral("Некорректный JSON от %1: %2")
                                    .arg(peerAddress(), error);
        m_socket->disconnectFromHost();
        return;
    }

    handleRequest(request);
}

void ClientSession::handleRequest(const QJsonObject &request)
{
    const QString requestId = request.value(QStringLiteral("id")).toString();
    const QString type = request.value(QStringLiteral("type")).toString();
    const QJsonObject data = request.value(QStringLiteral("data")).toObject();

    if (requestId.isEmpty() || type.isEmpty()) {
        sendJson(core::proto::errorReply(QString(), core::ErrorCode::BadRequest,
                                         QStringLiteral("Запрос без id или type")));
        return;
    }

    if (!m_core) {
        m_socket->disconnectFromHost();
        return;
    }

    const HandlerResult result = m_core->handle(type, data, this);    if (result.ok())
        sendJson(core::proto::successReply(requestId, result.data));
    else
        sendJson(core::proto::errorReply(requestId, result.code, result.message));
}

void ClientSession::onDisconnected()
{
    qInfo().noquote() << QStringLiteral("Отключение %1 (%2)")
                             .arg(peerAddress(), m_username.isEmpty() ? QStringLiteral("гость")
                                                                      : m_username);
    if (m_core)
        m_core->unregisterSession(this);
    deleteLater();
}

} // namespace server
