#include "data/AttachmentRepository.h"

#include "data/SqlUtil.h"

#include <QSqlQuery>
#include <QVariant>

namespace data {
namespace {

const char kSelectAttachment[] =
    "SELECT id, message_id, file_name, mime, byte_size, sha256, rel_path, thumb_path, "
    "       width, height, duration_ms "
    "FROM attachments ";

Attachment readAttachment(const QSqlQuery &query)
{
    Attachment attachment;
    attachment.id = query.value(QStringLiteral("id")).toLongLong();
    attachment.messageId = query.value(QStringLiteral("message_id")).toLongLong();
    attachment.fileName = query.value(QStringLiteral("file_name")).toString();
    attachment.mime = query.value(QStringLiteral("mime")).toString();
    attachment.byteSize = query.value(QStringLiteral("byte_size")).toLongLong();
    attachment.sha256 = query.value(QStringLiteral("sha256")).toString();
    attachment.relPath = query.value(QStringLiteral("rel_path")).toString();
    attachment.thumbPath = query.value(QStringLiteral("thumb_path")).toString();
    attachment.width = query.value(QStringLiteral("width")).toInt();
    attachment.height = query.value(QStringLiteral("height")).toInt();
    attachment.durationMs = query.value(QStringLiteral("duration_ms")).toInt();
    return attachment;
}

} // namespace

AttachmentRepository::AttachmentRepository(QSqlDatabase db)
    : m_db(std::move(db))
{
}

bool AttachmentRepository::insert(const Attachment &attachment, Attachment *stored,
                                  QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO attachments(message_id, file_name, mime, byte_size, sha256, rel_path, "
        "thumb_path, width, height, duration_ms) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(attachment.messageId);
    query.addBindValue(attachment.fileName);
    query.addBindValue(attachment.mime);
    query.addBindValue(attachment.byteSize);
    query.addBindValue(attachment.sha256);
    query.addBindValue(attachment.relPath);
    query.addBindValue(attachment.thumbPath.isEmpty() ? QVariant() : attachment.thumbPath);
    query.addBindValue(attachment.width > 0 ? attachment.width : QVariant());
    query.addBindValue(attachment.height > 0 ? attachment.height : QVariant());
    query.addBindValue(attachment.durationMs > 0 ? attachment.durationMs : QVariant());

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Сохранение вложения"));

    if (stored) {
        Attachment result = attachment;
        result.id = query.lastInsertId().toLongLong();
        *stored = result;
    }
    return true;
}

bool AttachmentRepository::byId(qint64 id, Attachment *out, bool *found, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QString::fromLatin1(kSelectAttachment) + QStringLiteral("WHERE id = ?"));
    query.addBindValue(id);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Поиск вложения"));

    const bool exists = query.next();
    if (found)
        *found = exists;
    if (exists && out)
        *out = readAttachment(query);
    return true;
}

bool AttachmentRepository::byMessageId(qint64 messageId, Attachment *out, bool *found,
                                      QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QString::fromLatin1(kSelectAttachment) + QStringLiteral("WHERE message_id = ?"));
    query.addBindValue(messageId);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Поиск вложения"));

    const bool exists = query.next();
    if (found)
        *found = exists;
    if (exists && out)
        *out = readAttachment(query);
    return true;
}

bool AttachmentRepository::markThumbnail(qint64 id, const QString &thumbPath, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE attachments SET thumb_path = ? WHERE id = ?"));
    query.addBindValue(thumbPath);
    query.addBindValue(id);

    if (!query.exec())
        return sql::failQuery(error, query, QStringLiteral("Обновление миниатюры"));
    return true;
}

} // namespace data
