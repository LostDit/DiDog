#include "app/AttachmentService.h"

#include "core/Limits.h"
#include "core/Protocol.h"
#include "net/ClientConnection.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QMimeDatabase>

namespace app {
namespace {

constexpr qint64 kPaceBytes = 1024 * 1024;

QString sha256OfFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();

    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

AttachmentService::AttachmentService(net::ClientConnection *connection, const QString &cacheRoot,
                                     QObject *parent)
    : QObject(parent)
    , m_connection(connection)
    , m_cacheRoot(cacheRoot)
{
    connect(m_connection, &net::ClientConnection::chunkReceived,
            this, &AttachmentService::onChunk);
    connect(m_connection, &net::ClientConnection::transferFinished,
            this, &AttachmentService::onTransferFinished);
    connect(m_connection, &net::ClientConnection::bytesWritten, this,
            [this](qint64) { pumpUpload(); });
}

AttachmentService::~AttachmentService()
{
    for (Upload &upload : m_uploads) {
        if (upload.file) {
            upload.file->close();
            delete upload.file;
            upload.file = nullptr;
        }
    }
    m_uploads.clear();

    for (Download &download : m_downloads) {
        if (download.file) {
            download.file->close();
            delete download.file;
            download.file = nullptr;
        }
        QFile::remove(download.tempPath);
    }
    m_downloads.clear();
}

data::MessageKind AttachmentService::kindForFile(const QString &filePath)
{
    const QString mime = mimeForFile(filePath);

    if (mime == QLatin1String("image/gif"))
        return data::MessageKind::Gif;
    if (mime.startsWith(QLatin1String("image/")))
        return data::MessageKind::Image;
    if (mime.startsWith(QLatin1String("video/")))
        return data::MessageKind::Video;
    return data::MessageKind::File;
}

QString AttachmentService::mimeForFile(const QString &filePath)
{
    QMimeDatabase database;
    return database.mimeTypeForFile(filePath, QMimeDatabase::MatchExtension).name();
}

void AttachmentService::upload(qint64 chatId, const QString &filePath)
{
    const QFileInfo info(filePath);

    if (!info.exists() || !info.isFile()) {
        emit uploadFailed(chatId, filePath, tr("Файл не найден."));
        return;
    }

    if (info.size() <= 0) {
        emit uploadFailed(chatId, filePath, tr("Файл пустой."));
        return;
    }

    if (info.size() > core::limits::kAttachmentMaxBytes) {
        emit uploadFailed(chatId, filePath, tr("Файл больше 25 МБ."));
        return;
    }

    if (m_uploads.contains(filePath))
        return;

    Upload upload;
    upload.chatId = chatId;
    upload.path = filePath;
    upload.fileName = info.fileName();
    upload.mime = mimeForFile(filePath);
    upload.size = info.size();
    upload.sha256 = sha256OfFile(filePath);
    upload.file = new QFile(filePath);

    if (upload.sha256.isEmpty() || !upload.file->open(QIODevice::ReadOnly)) {
        delete upload.file;
        emit uploadFailed(chatId, filePath, tr("Не удалось прочитать файл."));
        return;
    }

    m_uploads.insert(filePath, upload);
    startUpload(chatId, filePath, &m_uploads[filePath]);
}

void AttachmentService::startUpload(qint64 chatId, const QString &path, Upload *upload)
{
    Q_UNUSED(chatId)
    Q_UNUSED(path)

    QJsonObject request;
    request.insert(QStringLiteral("chat_id"), static_cast<double>(upload->chatId));
    request.insert(QStringLiteral("file_name"), upload->fileName);
    request.insert(QStringLiteral("mime"), upload->mime);
    request.insert(QStringLiteral("byte_size"), static_cast<double>(upload->size));
    request.insert(QStringLiteral("sha256"), upload->sha256);

    const QString filePath = upload->path;

    m_connection->request(QStringLiteral("attach.begin"), request,
                          [this, filePath](const QJsonObject &reply) {
                              auto it = m_uploads.find(filePath);
                              if (it == m_uploads.end())
                                  return;

                              if (!reply.value(QStringLiteral("ok")).toBool(false)) {
                                  dropUpload(filePath,
                                             core::proto::errorMessageOf(reply));
                                  return;
                              }

                              const QJsonObject data = reply.value(QStringLiteral("data")).toObject();
                              it->token = static_cast<quint32>(
                                  data.value(QStringLiteral("transfer_id")).toDouble());
                              m_tokenToPath.insert(it->token, filePath);

                              emit uploadProgress(it->chatId, filePath, 0);
                              pumpUpload();
                          },
                          30000);
}

void AttachmentService::pumpUpload()
{
    for (auto it = m_uploads.begin(); it != m_uploads.end(); ++it) {
        Upload &upload = it.value();

        if (upload.token == 0 || upload.finishing)
            continue;

        while (upload.sent < upload.size && m_connection->bytesToWrite() < kPaceBytes) {
            const QByteArray data = upload.file->read(core::limits::kChunkSizeBytes);
            if (data.isEmpty())
                break;

            if (!m_connection->sendChunk(upload.token, upload.sequence, data))
                break;

            upload.sent += data.size();
            ++upload.sequence;
        }

        const int percent = upload.size > 0
                                ? static_cast<int>(upload.sent * 100 / upload.size)
                                : 0;
        emit uploadProgress(upload.chatId, upload.path, percent);

        if (upload.sent >= upload.size)
            finishUpload(upload);
    }
}

void AttachmentService::finishUpload(Upload &upload)
{
    upload.finishing = true;

    const QString filePath = upload.path;
    const quint32 token = upload.token;

    QJsonObject request;
    request.insert(QStringLiteral("transfer_id"), static_cast<double>(token));

    m_connection->request(QStringLiteral("attach.finish"), request,
                          [this, filePath](const QJsonObject &reply) {
                              auto it = m_uploads.find(filePath);
                              if (it == m_uploads.end())
                                  return;

                              if (!reply.value(QStringLiteral("ok")).toBool(false)) {
                                  dropUpload(filePath, core::proto::errorMessageOf(reply));
                                  return;
                              }

                              const quint32 finishedToken = it->token;
                              const qint64 chatId = it->chatId;

                              if (it->file) {
                                  it->file->close();
                                  delete it->file;
                                  it->file = nullptr;
                              }

                              m_tokenToPath.remove(finishedToken);
                              m_uploads.erase(it);

                              emit uploaded(chatId, filePath, finishedToken);
                          },
                          60000);
}

void AttachmentService::dropUpload(const QString &path, const QString &message)
{
    auto it = m_uploads.find(path);
    if (it == m_uploads.end())
        return;

    const qint64 chatId = it->chatId;

    if (it->file) {
        it->file->close();
        delete it->file;
        it->file = nullptr;
    }
    m_tokenToPath.remove(it->token);
    m_uploads.erase(it);

    emit uploadFailed(chatId, path, message);
}

void AttachmentService::download(const data::Attachment &attachment)
{
    if (attachment.sha256.isEmpty())
        return;

    const QString cached = cachePathFor(attachment.sha256);
    if (QFileInfo::exists(cached)) {
        emit downloaded(attachment.id, cached);
        return;
    }

    for (const Download &download : qAsConst(m_downloads)) {
        if (download.attachmentId == attachment.id)
            return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("attachment_id"), static_cast<double>(attachment.id));

    m_connection->request(QStringLiteral("attach.get"), request,
                          [this, attachment](const QJsonObject &reply) {
                              if (!reply.value(QStringLiteral("ok")).toBool(false)) {
                                  emit downloadFailed(attachment.id,
                                                      core::proto::errorMessageOf(reply));
                                  return;
                              }

                              const QJsonObject data = reply.value(QStringLiteral("data")).toObject();
                              const quint32 transferId = static_cast<quint32>(
                                  data.value(QStringLiteral("transfer_id")).toDouble());

                              const QString tmpDir = m_cacheRoot + QStringLiteral("/.tmp");
                              QDir().mkpath(tmpDir);

                              Download download;
                              download.attachmentId = attachment.id;
                              download.sha256 = attachment.sha256;
                              download.size = static_cast<qint64>(
                                  data.value(QStringLiteral("byte_size")).toDouble());
                              download.tempPath = tmpDir + QLatin1Char('/')
                                                  + attachment.sha256 + QStringLiteral(".part");
                              download.file = new QFile(download.tempPath);

                              if (!download.file->open(QIODevice::WriteOnly)) {
                                  delete download.file;
                                  emit downloadFailed(attachment.id,
                                                      tr("Не удалось создать файл в кэше."));
                                  return;
                              }

                              m_downloads.insert(transferId, download);
                          },
                          30000);
}

void AttachmentService::onChunk(quint32 transferId, quint32 sequence, const QByteArray &data)
{
    Q_UNUSED(sequence)

    auto it = m_downloads.find(transferId);
    if (it == m_downloads.end())
        return;

    Download &download = it.value();
    if (!download.file)
        return;

    download.file->write(data);
    download.received += data.size();
}

void AttachmentService::onTransferFinished(quint32 transferId)
{
    auto it = m_downloads.find(transferId);
    if (it == m_downloads.end())
        return;

    Download download = it.value();
    m_downloads.erase(it);

    if (download.file) {
        download.file->close();
        delete download.file;
        download.file = nullptr;
    }

    QFile temp(download.tempPath);
    const QString actualSha = sha256OfFile(download.tempPath);

    if (actualSha.isEmpty() || actualSha.compare(download.sha256, Qt::CaseInsensitive) != 0) {
        QFile::remove(download.tempPath);
        emit downloadFailed(download.attachmentId, tr("Файл повреждён при передаче."));
        return;
    }

    const QString finalPath = cachePathFor(actualSha);
    QDir().mkpath(QFileInfo(finalPath).absolutePath());

    if (QFile::exists(finalPath))
        QFile::remove(download.tempPath);
    else if (!QFile::rename(download.tempPath, finalPath)) {
        QFile::remove(download.tempPath);
        emit downloadFailed(download.attachmentId, tr("Не удалось сохранить файл."));
        return;
    }

    emit downloaded(download.attachmentId, finalPath);
}

QString AttachmentService::cachePathFor(const QString &sha256) const
{
    return m_cacheRoot + QLatin1Char('/') + sha256.left(2) + QLatin1Char('/') + sha256;
}

bool AttachmentService::hasLocalCopy(const data::Attachment &attachment) const
{
    return !attachment.sha256.isEmpty() && QFileInfo::exists(cachePathFor(attachment.sha256));
}

QString AttachmentService::localPath(const data::Attachment &attachment) const
{
    return cachePathFor(attachment.sha256);
}

} // namespace app
