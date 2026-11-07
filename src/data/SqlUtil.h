#pragma once

#include <QSqlError>
#include <QSqlQuery>
#include <QString>

namespace data::sql {

inline bool fail(QString *error, const QString &text)
{
    if (error)
        *error = text;
    return false;
}

inline bool failQuery(QString *error, const QSqlQuery &query, const QString &context)
{
    if (error) {
        const QString details = query.lastError().text();
        *error = context.isEmpty() ? details
                                   : QStringLiteral("%1: %2").arg(context, details);
    }
    return false;
}

} // namespace data::sql
