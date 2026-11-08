#include "server/ClientSession.h"

#include "core/Limits.h"
#include "server/ServerCore.h"

#include <QDebug>
#include <QFile>
#include <QHostAddress>
#include <QJsonObject>
#include <QTcpSocket>

namespace server {
namespace {

constexpr qint64 kSocketHighWaterMark = 512 * 1024;

} // namespace

ClientSession::ClientSession(QTcpSocket *socket, ServerCore *core, QObject *parent)
    : QObject(parent)
    , m_socket(socket)
    , m_core(core)
{
    m_socket->setParent(this);
    connect(m_socket, &QTcpSocket::readyRead, this, &ClientSession::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientSession::onDisconnected);
    connect(m_socket, &QTcpSocket::bytesWritten, this, &ClientSession::onBytesWritten);
}

ClientSession::~ClientSession()
{
    closeDownloads();

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

void ClientSession::sendVoiceFrame(quint32 chatId, quint32 userId, quint32 sequence,
                                   const QByteArray &data)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    const QByteArray payload = core::proto::encodeVoice(chatId, userId, sequence, data);
    m_socket->write(core::FrameCodec::encode(core::FrameType::Voice, payload));
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
    if (frame.type == core::FrameType::Voice) {
        quint32 chatId = 0;
        quint32 userId = 0;
        quint32 sequence = 0;
        QByteArray data;
        if (!core::proto::decodeVoice(frame.payload, &chatId, &userId, &sequence, &data)) {
            qWarning().noquote() << QStringLiteral("Повреждённый голосовой кадр от %1")
                                        .arg(peerAddress());
            return;
        }

        if (m_core)
            m_core->handleVoiceFrame(this, chatId, sequence, data);
        return;
    }

    if (frame.type == core::FrameType::Binary) {
        quint32 transferId = 0;
        quint32 sequence = 0;
        QByteArray data;
        if (!core::decodeChunk(frame.payload, &transferId, &sequence, &data)) {
            qWarning().noquote() << QStringLiteral("Повреждённый чанк от %1").arg(peerAddress());
            m_socket->disconnectFromHost();
            return;
        }

        QString chunkError;
        if (m_core && !m_core->handleChunk(this, transferId, sequence, data, &chunkError)) {
            qWarning().noquote() << QStringLiteral("Чанк отклонён (%1): %2")
                                        .arg(peerAddress(), chunkError);
            m_socket->disconnectFromHost();
        }
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

    const HandlerResult result = m_core->handle(type, data, this);
    if (result.ok())
        sendJson(core::proto::successReply(requestId, result.data));
    else
        sendJson(core::proto::errorReply(requestId, result.code, result.message));

    // Ответ отправлен — можно выдавать файл чанками.
    startPendingDownloads();
}

void ClientSession::onBytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes)
    pumpDownloads();
}

quint32 ClientSession::allocateDownloadId()
{
    return m_nextDownloadId++;
}

void ClientSession::queueDownload(quint32 transferId, const QString &path, qint64 size)
{
    Download download;
    download.id = transferId;
    download.path = path;
    download.size = size;
    m_downloads.insert(transferId, download);
}

void ClientSession::startPendingDownloads()
{
    pumpDownloads();
}

void ClientSession::pumpDownloads()
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    // write() может синхронно вызвать bytesWritten → повторный вход в цикл,
    // который инвалидирует итератор QHash.
    if (m_pumping)
        return;

    struct Guard {
        bool &flag;
        ~Guard() { flag = false; }
    } guard{m_pumping};
    m_pumping = true;

    auto it = m_downloads.begin();
    while (it != m_downloads.end()) {
        Download &download = it.value();

        if (!download.started) {
            download.file = new QFile(download.path);
            if (!download.file->open(QIODevice::ReadOnly)) {
                qWarning().noquote() << QStringLiteral("Не удалось открыть вложение %1")
                                            .arg(download.path);
                delete download.file;
                download.file = nullptr;
                it = m_downloads.erase(it);
                continue;
            }
            download.started = true;
        }

        while (download.sent < download.size
               && m_socket->bytesToWrite() < kSocketHighWaterMark) {
            const qint64 left = download.size - download.sent;
            const QByteArray data = download.file->read(
                static_cast<qint64>(core::limits::kChunkSizeBytes));

            if (data.isEmpty())
                break;

            const qint64 payloadSize = qMin<qint64>(data.size(), left);
            const QByteArray chunk = core::encodeChunk(download.id, download.sequence,
                                                       data.left(static_cast<int>(payloadSize)));
            m_socket->write(core::FrameCodec::encode(core::FrameType::Binary, chunk));
            download.sent += payloadSize;
            ++download.sequence;

            if (payloadSize < data.size())
                break;
        }

        if (download.sent >= download.size) {
            QJsonObject payload;
            payload.insert(QStringLiteral("transfer_id"), static_cast<double>(download.id));

            QJsonObject end;
            end.insert(QStringLiteral("v"), core::proto::kVersion);
            end.insert(QStringLiteral("type"), QStringLiteral("attach.end"));
            end.insert(QStringLiteral("data"), payload);
            m_socket->write(
                core::FrameCodec::encode(core::FrameType::Json, core::proto::encode(end)));

            download.file->close();
            delete download.file;
            download.file = nullptr;
            it = m_downloads.erase(it);
            continue;
        }

        ++it;
    }
}

void ClientSession::closeDownloads()
{
    for (Download &download : m_downloads) {
        if (download.file) {
            download.file->close();
            delete download.file;
            download.file = nullptr;
        }
    }
    m_downloads.clear();
}

void ClientSession::onDisconnected()
{
    qInfo().noquote() << QStringLiteral("Отключение %1 (%2)")
                             .arg(peerAddress(), m_username.isEmpty() ? QStringLiteral("гость")
                                                                      : m_username);
    if (m_core)
        m_core->unregisterSession(this);
    deleteLater();}

} // namespace server
