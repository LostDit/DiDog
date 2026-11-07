#include "ui/AvatarPainter.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>

namespace ui {

QString avatarInitials(const QString &displayName)
{
    const QString simplified = displayName.simplified();
    if (simplified.isEmpty())
        return QStringLiteral("?");

    const QStringList parts = simplified.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString initials;
    for (const QString &part : parts) {
        if (initials.size() >= 2)
            break;
        initials += part.at(0).toUpper();
    }
    return initials.isEmpty() ? QStringLiteral("?") : initials;
}

QColor avatarColor(int hue)
{
    const int normalized = ((hue % 360) + 360) % 360;
    QColor color = QColor::fromHsl(normalized, 168, 122);
    return color.isValid() ? color : QColor(QStringLiteral("#3B7DF6"));
}

void paintAvatar(QPainter *painter, const QRectF &rect, const QString &initials, int hue)
{
    if (!painter || rect.isEmpty())
        return;

    const QColor base = avatarColor(hue);

    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    gradient.setColorAt(0.0, base.lighter(114));
    gradient.setColorAt(1.0, base.darker(112));

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(gradient);
    painter->drawEllipse(rect.adjusted(0.5, 0.5, -0.5, -0.5));

    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(255, 255, 255, 38), 1.0));
    painter->drawEllipse(rect.adjusted(1.0, 1.0, -1.0, -1.0));

    QFont font = painter->font();
    font.setBold(true);
    font.setPixelSize(qMax(9, static_cast<int>(rect.height() * 0.38)));
    painter->setFont(font);
    painter->setPen(QColor(255, 255, 255, 240));
    painter->drawText(rect, Qt::AlignCenter, initials);
    painter->restore();
}

} // namespace ui
