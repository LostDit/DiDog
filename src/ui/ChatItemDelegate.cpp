#include "ui/ChatItemDelegate.h"

#include "app/ThemeManager.h"
#include "ui/AvatarPainter.h"
#include "ui/ChatListModel.h"
#include "ui/Formatting.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QStyle>

namespace ui {
namespace {

QColor blend(const QColor &foreground, double alpha, const QColor &background)
{
    const double inverse = 1.0 - alpha;
    return QColor(int(foreground.red() * alpha + background.red() * inverse),
                  int(foreground.green() * alpha + background.green() * inverse),
                  int(foreground.blue() * alpha + background.blue() * inverse));
}

// Цвета берём из активной темы: QSS не влияет на ручную отрисовку делегата.
struct DelegateColors {
    QColor surface;
    QColor text;
    QColor muted;
    QColor faint;
    QColor accent;
    QColor hover;
    QColor onAccent;

    explicit DelegateColors(const QPalette &palette)
    {
        auto *theme = app::ThemeManager::instance();
        const auto pick = [theme](const char *token, const QColor &fallback) {
            if (!theme)
                return fallback;
            const QColor color = theme->color(QString::fromLatin1(token));
            return color.isValid() ? color : fallback;
        };

        surface = pick("surface", palette.base().color());
        text = pick("text", palette.text().color());
        muted = pick("text-muted", palette.placeholderText().color());
        faint = pick("text-faint", muted);
        accent = pick("accent", palette.highlight().color());
        hover = pick("hover", palette.alternateBase().color());
        onAccent = Qt::white;
    }
};

} // namespace

ChatItemDelegate::ChatItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void ChatItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    const QVariant value = index.data(ChatListModel::SummaryRole);
    if (!value.canConvert<data::ChatSummary>())
        return;

    const data::ChatSummary summary = value.value<data::ChatSummary>();
    const DelegateColors colors(option.palette);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect row = option.rect.adjusted(6, 3, -6, -3);

    QColor background = Qt::transparent;
    if (option.state & QStyle::State_Selected)
        background = blend(colors.accent, 0.16, colors.surface);
    else if (option.state & QStyle::State_MouseOver)
        background = colors.hover;

    if (background.alpha() > 0) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(background);
        painter->drawRoundedRect(row, 12, 12);
    }

    // Аватар
    const QRectF avatarRect(row.left() + 10, row.center().y() - 22 + 1, 44, 44);
    paintAvatar(painter, avatarRect, avatarInitials(summary.peerDisplayName), summary.peerHue);

    const int textLeft = int(avatarRect.right()) + 12;
    const int timeWidth = 52;
    const int textWidth = row.right() - textLeft - timeWidth - 8;

    // Время
    QFont timeFont = option.font;
    timeFont.setPointSizeF(qMax(8.0, option.font.pointSizeF() - 1.5));
    painter->setFont(timeFont);
    painter->setPen(colors.faint);
    painter->drawText(QRect(row.right() - timeWidth - 4, row.top() + 12, timeWidth, 16),
                      Qt::AlignRight | Qt::AlignVCenter,
                      summary.lastAt > 0 ? format::chatTime(summary.lastAt) : QString());

    // Имя
    QFont nameFont = option.font;
    nameFont.setBold(true);
    painter->setFont(nameFont);
    painter->setPen(colors.text);
    const QFontMetrics nameMetrics(nameFont);

    int nameRight = textWidth;
    if (summary.chat.kind == QLatin1String("group")) {
        const QString chip = tr("группа");
        QFont chipFont = option.font;
        chipFont.setPointSizeF(qMax(7.5, option.font.pointSizeF() - 2.0));
        const QFontMetrics chipMetrics(chipFont);
        const int chipWidth = chipMetrics.horizontalAdvance(chip) + 12;
        const QRectF chipRect(textLeft + textWidth - chipWidth, row.top() + 13, chipWidth, 16);

        painter->setPen(Qt::NoPen);
        painter->setBrush(blend(colors.accent, 0.14, colors.surface));
        painter->drawRoundedRect(chipRect, 8, 8);

        painter->setFont(chipFont);
        painter->setPen(colors.accent);
        painter->drawText(chipRect, Qt::AlignCenter, chip);

        painter->setFont(nameFont);
        painter->setPen(colors.text);
        nameRight = textWidth - chipWidth - 8;
    }

    painter->drawText(QRect(textLeft, row.top() + 12, nameRight, 18),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      nameMetrics.elidedText(summary.peerDisplayName, Qt::ElideRight, nameRight));

    // Превью
    QFont previewFont = option.font;
    previewFont.setPointSizeF(qMax(8.0, option.font.pointSizeF() - 0.5));
    painter->setFont(previewFont);
    painter->setPen(summary.unread > 0 ? colors.text : colors.muted);

    QString preview = format::previewText(summary.lastKind, summary.lastBody, false);
    if (preview.isEmpty())
        preview = QStringLiteral("Нет сообщений");

    const QFontMetrics previewMetrics(previewFont);
    const int previewWidth = textWidth - (summary.unread > 0 ? 26 : 0);
    painter->drawText(QRect(textLeft, row.top() + 34, previewWidth, 18),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      previewMetrics.elidedText(preview, Qt::ElideRight, previewWidth));

    // Бейдж непрочитанных
    if (summary.unread > 0) {
        const int count = qMin(summary.unread, 99);
        const QString label = count >= 99 ? QStringLiteral("99+") : QString::number(count);
        QFont badgeFont = option.font;
        badgeFont.setPointSizeF(qMax(7.5, option.font.pointSizeF() - 2.0));
        badgeFont.setBold(true);

        const QFontMetrics badgeMetrics(badgeFont);
        const int width = qMax(20, badgeMetrics.horizontalAdvance(label) + 12);
        const QRectF badge(row.right() - width - 8, row.bottom() - 26, width, 20);

        painter->setPen(Qt::NoPen);
        painter->setBrush(colors.accent);
        painter->drawRoundedRect(badge, 10, 10);

        painter->setFont(badgeFont);
        painter->setPen(colors.onAccent);
        painter->drawText(badge, Qt::AlignCenter, label);
    }

    painter->restore();
}

QSize ChatItemDelegate::sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(0, 74);
}

} // namespace ui
