#include "ui/MessageBubble.h"

#include "ui/AvatarWidget.h"
#include "ui/Formatting.h"

#include <QFrame>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QVBoxLayout>

namespace ui {
namespace {

// Экранирует текст и превращает http(s)-ссылки в кликабельные.
QString richText(const QString &raw)
{
    QString escaped = raw.toHtmlEscaped();
    escaped.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));

    static const QRegularExpression linkPattern(
        QStringLiteral("(https?://[^\\s<]+)"));
    escaped.replace(linkPattern, QStringLiteral("<a href=\"\\1\">\\1</a>"));
    return escaped;
}

QString statusText(data::DeliveryState state)
{
    switch (state) {
    case data::DeliveryState::Pending:   return QStringLiteral("·");
    case data::DeliveryState::Sent:      return QStringLiteral("✓");
    case data::DeliveryState::Delivered: return QStringLiteral("✓✓");
    case data::DeliveryState::Read:      return QStringLiteral("✓✓");
    case data::DeliveryState::Failed:    return QStringLiteral("!");
    }
    return QString();
}

} // namespace

MessageBubble::MessageBubble(const data::Message &message, bool own, bool showAvatar,
                             QWidget *parent)
    : QWidget(parent)
    , m_message(message)
    , m_own(own)
    , m_showAvatar(showAvatar)
{
    setObjectName(QStringLiteral("MessageBubble"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 2, 12, 2);
    layout->setSpacing(8);

    if (!m_own) {
        auto *avatar = new AvatarWidget(this);
        avatar->setDiameter(36);
        avatar->setIdentity(message.senderDisplayName, message.senderHue);
        avatar->setVisible(m_showAvatar);
        layout->addWidget(avatar, 0, Qt::AlignTop);
    } else {
        layout->addStretch(1);
    }

    m_bubble = new QFrame(this);
    m_bubble->setObjectName(m_own ? QStringLiteral("BubbleOwn")
                                  : QStringLiteral("BubbleOther"));
    m_bubble->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    auto *bubbleLayout = new QVBoxLayout(m_bubble);
    bubbleLayout->setContentsMargins(12, 8, 12, 6);
    bubbleLayout->setSpacing(2);

    m_text = new QLabel(m_bubble);
    m_text->setObjectName(QStringLiteral("MessageText"));
    m_text->setTextFormat(Qt::RichText);
    m_text->setText(richText(message.body));
    m_text->setWordWrap(true);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    m_text->setOpenExternalLinks(true);
    m_text->setVisible(!message.body.isEmpty());
    bubbleLayout->addWidget(m_text);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(6);
    footer->addStretch(1);

    m_time = new QLabel(format::clockTime(message.createdAt), m_bubble);
    m_time->setObjectName(QStringLiteral("MessageTime"));
    footer->addWidget(m_time);

    m_status = new QLabel(statusText(message.state), m_bubble);
    m_status->setObjectName(QStringLiteral("MessageStatus"));
    m_status->setProperty("state", data::deliveryStateToString(message.state));
    footer->addWidget(m_status);

    m_retry = new QPushButton(tr("Повторить"), m_bubble);
    m_retry->setObjectName(QStringLiteral("LinkButton"));
    m_retry->setCursor(Qt::PointingHandCursor);
    m_retry->setVisible(false);
    connect(m_retry, &QPushButton::clicked, this, [this]() {
        emit retryRequested(m_message.clientMsgId);
    });
    footer->addWidget(m_retry);

    bubbleLayout->addLayout(footer);

    layout->addWidget(m_bubble, 0, Qt::AlignTop);

    if (!m_own)
        layout->addStretch(1);

    adjustTextWidth();
    updateStatus();
}

void MessageBubble::setMaxBubbleWidth(int width)
{
    if (!m_bubble)
        return;

    m_maxTextWidth = qBound(120, width - 24, 720);
    m_bubble->setMaximumWidth(m_maxTextWidth + 24);
    adjustTextWidth();
}

void MessageBubble::adjustTextWidth()
{
    if (!m_text)
        return;

    const QFontMetrics metrics(m_text->font());

    int natural = 0;
    const QStringList lines = m_message.body.split(QLatin1Char('\n'));
    for (const QString &line : lines)
        natural = qMax(natural, metrics.horizontalAdvance(line));

    const int width = qBound(60, natural + 4, m_maxTextWidth);
    m_text->setFixedWidth(width);
}

void MessageBubble::updateMessage(const data::Message &message)
{
    const bool textChanged = (m_message.body != message.body);
    m_message = message;

    if (textChanged && m_text) {
        m_text->setText(richText(message.body));
        m_text->setVisible(!message.body.isEmpty());
        adjustTextWidth();
    }

    updateStatus();
}

void MessageBubble::rebuild()
{
    if (m_text)
        m_text->setText(richText(m_message.body));
    updateStatus();
}

void MessageBubble::updateStatus()
{
    if (!m_status)
        return;

    const bool failed = (m_message.state == data::DeliveryState::Failed);
    m_status->setText(statusText(m_message.state));
    m_status->setProperty("state", data::deliveryStateToString(m_message.state));
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
    // Статус доставки имеет смысл только у своих сообщений.
    m_status->setVisible(m_own && !failed);

    if (m_retry)
        m_retry->setVisible(m_own && failed);

    if (m_time && m_message.createdAt > 0)
        m_time->setText(format::clockTime(m_message.createdAt));

    if (failed)
        setToolTip(tr("Сообщение не отправлено"));
}

} // namespace ui
