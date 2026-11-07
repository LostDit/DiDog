#pragma once

#include "data/Models.h"

#include <QHash>
#include <QObject>
#include <QString>

class QFile;

namespace net {
class ClientConnection;
}

namespace app {

// Загрузка и скачивание вложений поверх ClientConnection.
// Файлы кэшируются по sha256: <cacheRoot>/<ab>/<sha256>.
class AttachmentService : public QObject
{
    Q_OBJECT

public:
    AttachmentService(net::ClientConnection *connection, const QString &cacheRoot,
                      QObject *parent = nullptr);
    ~AttachmentService() override;

    void upload(qint64 chatId, const QString &filePath);
    void download(const data::Attachment &attachment);

    bool hasLocalCopy(const data::Attachment &attachment) const;
    QString localPath(const data::Attachment &attachment) const;

    static data::MessageKind kindForFile(const QString &filePath);
    static QString mimeForFile(const QString &filePath);

signals:
    void uploadProgress(qint64 chatId, const QString &filePath, int percent);
    void uploaded(qint64 chatId, const QString &filePath, quint32 token);
    void uploadFailed(qint64 chatId, const QString &filePath, const QString &message);
    void downloaded(qint64 attachmentId, const QString &localPath);
    void downloadFailed(qint64 attachmentId, const QString &message);

private:
    struct Upload {
        qint64 chatId = 0;
        QString path;
        QString fileName;
        QString mime;
        QString sha256;
        qint64 size = 0;
        qint64 sent = 0;
        quint32 token = 0;
        quint32 sequence = 0;
        QFile *file = nullptr;
        bool finishing = false;
    };

    struct Download {
        qint64 attachmentId = 0;
        QString sha256;
        qint64 size = 0;
        qint64 received = 0;
        QFile *file = nullptr;
        QString tempPath;
    };

    void startUpload(qint64 chatId, const QString &path, Upload *upload);
    void pumpUpload();
    void finishUpload(Upload &upload);
    void dropUpload(const QString &path, const QString &message);

    void onChunk(quint32 transferId, quint32 sequence, const QByteArray &data);
    void onTransferFinished(quint32 transferId);

    QString cachePathFor(const QString &sha256) const;

    net::ClientConnection *m_connection = nullptr;
    QString m_cacheRoot;

    QHash<QString, Upload> m_uploads;      // ключ — путь к файлу
    QHash<quint32, QString> m_tokenToPath; // token → путь (для прогресса)
    QHash<quint32, Download> m_downloads;  // transferId → приём
};

} // namespace app
