#pragma once

#include <QColor>
#include <QString>

class QPainter;
class QRectF;

namespace ui {

// Инициалы для fallback-аватара: «Аня Петрова» → «АП», «Аня» → «А».
QString avatarInitials(const QString &displayName);

// Базовый цвет аватара по hue (0..359).
QColor avatarColor(int hue);

// Рисует круглый аватар с градиентом и инициалами.
void paintAvatar(QPainter *painter, const QRectF &rect, const QString &initials, int hue);

} // namespace ui
