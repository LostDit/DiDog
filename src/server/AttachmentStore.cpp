#include "server/AttachmentStore.h"

#include "core/Ids.h"
#include "core/Limits.h"
#include "security/Validator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

namespace server {

AttachmentStore::AttachmentStore(const QString &rootDir)
    : m_root(rootDir)
{
}

quint32 AttachmentStore::begin(qint64 userId, const QString &fileName, const QString &mime,
                               qint64 byteSize, const QString &sha256, int *chunkSize,
                               QString *error)
{
    if (byteSize <= 0 || byteSize > core::limits::kAttachmentMaxBytes) {
        if (error)
            *error = QStringLiteral("Размер вложения вне допустимого диапазона");
        return 0;
    }

    if (!security::isValidSha256(sha256)) {
        if (error)
            *error = QStringLiteral("Некорректный sha256");
        return 0;
    }

    const security::CheckResult nameCheck = security::checkAttachmentName(fileName);
    if (!nameCheck.ok) {
        if (error)
            *error = nameCheck.message;
        return 0;
    }

    QDir root(m_root);
    if (!root.mkpath(QStringLiteral(".tmp"))) {
        if (error)
            *error = QStringLiteral("Не удалось создать каталог для передачи");
        return 0;
    }

    auto *transfer = new Transfer;
    transfer->id = m_nextId++;
    transfer->userId = userId;
    transfer->fileName = security::safeFileName(fileName);
    transfer->mime = mime;
    transfer->expectedSha = sha256.toLower();
    transfer->expectedSize = byteSize;
    transfer->tempPath = root.filePath(QStringLiteral(".tmp/%1.part").arg(core::newUuid()));

    transfer->file = std::make_unique<QFile>(transfer->tempPath);
    if (!transfer->file->open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Не удалось создать временный файл");
        delete transfer;
        return 0;
    }

    const quint32 id = transfer->id;
    m_transfers.insert(id, transfer);

    if (chunkSize)
        *chunkSize = static_cast<int>(core::limits::kChunkSizeBytes);

    return id;
}

bool AttachmentStore::writeChunk(qint64 userId, quint32 transferId, quint32 sequence,
                                 const QByteArray &data, QString *error)
{
    auto it = m_transfers.find(transferId);
    if (it == m_transfers.end()) {
        if (error)
            *error = QStringLiteral("Передача не найдена");
        return false;
    }

    Transfer *transfer = it.value();

    if (transfer->userId != userId) {
        if (error)
            *error = QStringLiteral("Передача принадлежит другому пользователю");
        return false;
    }

    if (sequence != transfer->nextSequence) {
        if (error) {
            *error = QStringLiteral("Нарушен порядок чанков: ожидался %1, получен %2")
                         .arg(transfer->nextSequence)
                         .arg(sequence);
        }
        return false;
    }

    if (transfer->written + data.size() > transfer->expectedSize) {
        if (error)
            *error = QStringLiteral("Получено больше байт, чем заявлено");
        return false;
    }

    if (transfer->file->write(data) != data.size()) {
        if (error)
            *error = QStringLiteral("Ошибка записи вложения на диск");
        return false;
    }

    transfer->written += data.size();
    ++transfer->nextSequence;
    return true;
}

bool AttachmentStore::finish(qint64 userId, quint32 transferId, ReadyAttachment *out,
                             QString *error)
{
    auto it = m_transfers.find(transferId);
    if (it == m_transfers.end()) {
        if (error)
            *error = QStringLiteral("Передача не найдена");
        return false;
    }

    Transfer *transfer = it.value();

    if (transfer->userId != userId) {
        if (error)
            *error = QStringLiteral("Передача принадлежит другому пользователю");
        discard(transferId);
        return false;
    }

    if (transfer->written != transfer->expectedSize) {
        if (error) {
            *error = QStringLiteral("Размер не совпадает: получено %1 из %2")
                         .arg(transfer->written)
                         .arg(transfer->expectedSize);
        }
        discard(transferId);
        return false;
    }

    transfer->file->close();

    const QString actualSha = sha256OfFile(transfer->tempPath);
    if (actualSha.compare(transfer->expectedSha, Qt::CaseInsensitive) != 0) {
        if (error)
            *error = QStringLiteral("Контрольная сумма не совпадает");
        discard(transferId);
        return false;
    }

    const QString relPath = QStringLiteral("%1/%2").arg(actualSha.left(2), actualSha);
    const QString destination = absolutePath(relPath);

    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        if (error)
            *error = QStringLiteral("Не удалось создать каталог хранилища");
        discard(transferId);
        return false;
    }

    // Тот же файл уже мог быть загружен ранее — тогда просто убираем копию.
    if (QFile::exists(destination)) {
        QFile::remove(transfer->tempPath);
    } else if (!QFile::rename(transfer->tempPath, destination)) {
        if (error)
            *error = QStringLiteral("Не удалось переместить вложение в хранилище");
        discard(transferId);
        return false;
    }

    if (out) {
        out->token = transferId;
        out->userId = userId;
        out->fileName = transfer->fileName;
        out->mime = transfer->mime;
        out->sha256 = actualSha;
        out->relPath = relPath;
        out->byteSize = transfer->written;
        out->consumed = false;
    }

    m_transfers.erase(it);
    delete transfer;
    return true;
}

void AttachmentStore::abortUser(qint64 userId)
{
    const auto ids = m_transfers.keys();
    for (quint32 id : ids) {
        auto it = m_transfers.find(id);
        if (it != m_transfers.end() && it.value()->userId == userId)
            discard(id);
    }
}

void AttachmentStore::abortAll()
{
    const auto ids = m_transfers.keys();
    for (quint32 id : ids)
        discard(id);
}

QString AttachmentStore::absolutePath(const QString &relPath) const
{
    return QDir(m_root).filePath(relPath);
}

bool AttachmentStore::contains(const QString &relPath) const
{
    return QFileInfo::exists(absolutePath(relPath));
}

QString AttachmentStore::sha256OfFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();

    return QString::fromLatin1(hash.result().toHex());
}

void AttachmentStore::discard(quint32 transferId)
{
    auto it = m_transfers.find(transferId);
    if (it == m_transfers.end())
        return;

    Transfer *transfer = it.value();
    m_transfers.erase(it);

    if (transfer->file && transfer->file->isOpen())
        transfer->file->close();
    QFile::remove(transfer->tempPath);
    delete transfer;
}

} // namespace server
