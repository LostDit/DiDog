#include "ui/AvatarWidget.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>

namespace ui {

AvatarWidget::AvatarWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void AvatarWidget::setIdentity(const QString &displayName, int hue)
{
    m_initials = initialsFor(displayName);
    m_hue = ((hue % 360) + 360) % 360;
    update();
}

void AvatarWidget::setDiameter(int diameter)
{
    const int clamped = qBound(16, diameter, 128);
    if (clamped == m_diameter)
        return;

    m_diameter = clamped;
    setFixedSize(m_diameter, m_diameter);
    updateGeometry();
    update();
}

QSize AvatarWidget::sizeHint() const
{
    return QSize(m_diameter, m_diameter);
}

QSize AvatarWidget::minimumSizeHint() const
{
    return QSize(16, 16);
}

void AvatarWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF circle = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    QColor base = QColor::fromHsl(m_hue, 168, 122);
    if (!base.isValid())
        base = QColor("#3B7DF6");

    QLinearGradient gradient(circle.topLeft(), circle.bottomRight());
    gradient.setColorAt(0.0, base.lighter(114));
    gradient.setColorAt(1.0, base.darker(112));

    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawEllipse(circle);

    // Тонкое светлое кольцо — читается на любом фоне.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255, 38), 1.0));
    painter.drawEllipse(circle.adjusted(0.5, 0.5, -0.5, -0.5));

    QFont textFont = font();
    textFont.setBold(true);
    textFont.setPixelSize(qMax(9, static_cast<int>(m_diameter * 0.38)));
    painter.setFont(textFont);
    painter.setPen(QColor(255, 255, 255, 240));
    painter.drawText(rect(), Qt::AlignCenter, m_initials);
}

QString AvatarWidget::initialsFor(const QString &displayName)
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

} // namespace ui
