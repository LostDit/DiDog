#pragma once

#include "data/Models.h"

#include <QString>

namespace ui::format {

// Время в списке чатов: «12:34», «Вчера», «07.11».
QString chatTime(qint64 unixMs);

// Время в пузыре: «12:34».
QString clockTime(qint64 unixMs);

// Разделитель дат: «Сегодня», «Вчера», «7 ноября 2026».
QString dayLabel(qint64 unixMs);

bool sameDay(qint64 leftMs, qint64 rightMs);

// Короткий текст последнего сообщения с учётом типа и автора.
QString previewText(data::MessageKind kind, const QString &body, bool own);

// Размер файла: «12,3 КБ».
QString fileSize(qint64 bytes);

} // namespace ui::format
