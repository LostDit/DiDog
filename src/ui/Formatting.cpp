#include "ui/Formatting.h"

#include <QDateTime>
#include <QLocale>

namespace ui::format {
namespace {

const char *const kMonthsGenitive[] = {
    "января", "февраля", "марта", "апреля", "мая", "июня",
    "июля", "августа", "сентября", "октября", "ноября", "декабря"
};

QDate dateOf(qint64 unixMs)
{
    return QDateTime::fromMSecsSinceEpoch(unixMs).date();
}

} // namespace

QString chatTime(qint64 unixMs)
{
    const QDateTime moment = QDateTime::fromMSecsSinceEpoch(unixMs);
    const QDate today = QDate::currentDate();

    if (moment.date() == today)
        return moment.toString(QStringLiteral("HH:mm"));

    if (moment.date() == today.addDays(-1))
        return QStringLiteral("Вчера");

    if (moment.date().year() == today.year())
        return moment.toString(QStringLiteral("dd.MM"));

    return moment.toString(QStringLiteral("dd.MM.yy"));
}

QString clockTime(qint64 unixMs)
{
    return QDateTime::fromMSecsSinceEpoch(unixMs).toString(QStringLiteral("HH:mm"));
}

QString dayLabel(qint64 unixMs)
{
    const QDate date = dateOf(unixMs);
    const QDate today = QDate::currentDate();

    if (date == today)
        return QStringLiteral("Сегодня");
    if (date == today.addDays(-1))
        return QStringLiteral("Вчера");

    const QString month = QString::fromUtf8(kMonthsGenitive[qBound(0, date.month() - 1, 11)]);
    if (date.year() == today.year())
        return QStringLiteral("%1 %2").arg(date.day()).arg(month);

    return QStringLiteral("%1 %2 %3").arg(date.day()).arg(month).arg(date.year());
}

bool sameDay(qint64 leftMs, qint64 rightMs)
{
    return dateOf(leftMs) == dateOf(rightMs);
}

QString previewText(data::MessageKind kind, const QString &body, bool own)
{
    QString prefix = own ? QStringLiteral("Вы: ") : QString();

    switch (kind) {
    case data::MessageKind::Image:
        return prefix + QStringLiteral("Фотография");
    case data::MessageKind::Video:
        return prefix + QStringLiteral("Видео");
    case data::MessageKind::Gif:
        return prefix + QStringLiteral("GIF");
    case data::MessageKind::File:
        return prefix + QStringLiteral("Файл");
    case data::MessageKind::Text:
        break;
    }

    return prefix + body.simplified();
}

QString fileSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 Б").arg(bytes);

    const double kb = bytes / 1024.0;
    if (kb < 1024.0)
        return QLocale().toString(kb, 'f', 1) + QStringLiteral(" КБ");

    const double mb = kb / 1024.0;
    return QLocale().toString(mb, 'f', 1) + QStringLiteral(" МБ");
}

} // namespace ui::format
