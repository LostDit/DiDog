#pragma once

#include "data/Models.h"

#include <QSqlDatabase>
#include <QString>

namespace data {

class AttachmentRepository
{
public:
    explicit AttachmentRepository(QSqlDatabase db);

    bool insert(const Attachment &attachment, Attachment *stored, QString *error);
    bool byId(qint64 id, Attachment *out, bool *found, QString *error) const;
    bool byMessageId(qint64 messageId, Attachment *out, bool *found, QString *error) const;
    bool markThumbnail(qint64 id, const QString &thumbPath, QString *error);

private:
    QSqlDatabase m_db;
};

} // namespace data
