#pragma once

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QString>

#include <memory>

namespace server {

// Метаданные принятого, но ещё не привязанного к сообщению файла.
struct ReadyAttachment {
    quint32 token = 0;
    qint64 userId = 0;
    QString fileName;
    QString mime;
    QString sha256;
    QString relPath;
    qint64 byteSize = 0;
    bool consumed = false;
};

// Приём вложений: временный файл → проверка размера и sha256 → контент-адресное
// хранилище <root>/<ab>/<sha256>. Путь никогда не строится из имени пользователя.
class AttachmentStore
{
public:
    explicit AttachmentStore(const QString &rootDir);

    void setRoot(const QString &rootDir) { m_root = rootDir; }

    // Начинает передачу. chunkSize — рекомендуемый размер чанка.
    quint32 begin(qint64 userId, const QString &fileName, const QString &mime, qint64 byteSize,
                  const QString &sha256, int *chunkSize, QString *error);

    bool writeChunk(qint64 userId, quint32 transferId, quint32 sequence, const QByteArray &data,
                    QString *error);

    // Завершает передачу: проверяет размер и хеш, переносит файл в хранилище.
    bool finish(qint64 userId, quint32 transferId, ReadyAttachment *out, QString *error);

    void abortUser(qint64 userId);
    void abortAll();

    QString absolutePath(const QString &relPath) const;
    bool contains(const QString &relPath) const;

    // Потоковый подсчёт sha256 файла (используется и при проверке, и при выдаче).
    static QString sha256OfFile(const QString &path);

private:
    struct Transfer {
        quint32 id = 0;
        qint64 userId = 0;
        QString fileName;
        QString mime;
        QString expectedSha;
        qint64 expectedSize = 0;
        qint64 written = 0;
        quint32 nextSequence = 0;
        std::unique_ptr<QFile> file;
        QString tempPath;
    };

    void discard(quint32 transferId);

    QString m_root;
    quint32 m_nextId = 1;
    QHash<quint32, Transfer *> m_transfers;
};

} // namespace server
