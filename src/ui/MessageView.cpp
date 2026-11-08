#include "ui/MessageView.h"

#include "ui/Formatting.h"
#include "ui/MessageBubble.h"

#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace ui {
namespace {

constexpr qint64 kGroupWindowMs = 5 * 60 * 1000;

} // namespace

MessageView::MessageView(QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("MessageArea"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_content = new QWidget;
    m_content->setObjectName(QStringLiteral("MessageContent"));

    m_layout = new QVBoxLayout(m_content);
    m_layout->setContentsMargins(0, 12, 0, 12);
    m_layout->setSpacing(0);

    m_loading = new QLabel(tr("Загрузка истории…"), m_content);
    m_loading->setObjectName(QStringLiteral("DayChip"));
    m_loading->setAlignment(Qt::AlignCenter);
    m_loading->hide();
    m_layout->addWidget(m_loading, 0, Qt::AlignHCenter);

    m_layout->addStretch(1);

    setWidget(m_content);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value == 0 && m_hasMore)
            emit loadOlderRequested();
    });
}

bool MessageView::isAtBottom() const
{
    const QScrollBar *bar = verticalScrollBar();
    return bar->value() >= bar->maximum() - 4;
}

void MessageView::scrollToBottom()
{
    QTimer::singleShot(0, this, [this]() {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    });
}

bool MessageView::scrollToMessage(qint64 messageId)
{
    for (MessageBubble *bubble : qAsConst(m_bubbles)) {
        if (bubble->messageId() != messageId)
            continue;

        ensureWidgetVisible(bubble, 0, 60);
        return true;
    }
    return false;
}

void MessageView::setLoading(bool loading)
{
    if (!m_loading)
        return;

    m_loading->setVisible(loading);
}

void MessageView::setMessages(const QList<data::Message> &messages)
{
    setLoading(false);
    const bool wasAtBottom = isAtBottom();
    const int oldMaximum = verticalScrollBar()->maximum();
    const int oldValue = verticalScrollBar()->value();

    QString newLastKey;
    if (!messages.isEmpty()) {
        const data::Message &last = messages.last();
        newLastKey = last.id > 0 ? QString::number(last.id) : last.clientMsgId;
    }

    const bool appended = !m_firstLoad && !newLastKey.isEmpty() && newLastKey != m_lastKey
                          && !m_lastKey.isEmpty() && wasAtBottom;

    clearContent();

    qint64 previousTime = 0;
    qint64 previousSender = 0;

    for (int i = 0; i < messages.size(); ++i) {
        const data::Message &message = messages.at(i);
        const bool newDay = (previousTime == 0)
                            || !format::sameDay(previousTime, message.createdAt);

        if (newDay)
            addDaySeparator(message.createdAt);

        const bool own = message.senderId == m_currentUserId;
        const bool showAvatar = !own
                               && (newDay || previousSender != message.senderId
                                   || message.createdAt - previousTime > kGroupWindowMs);

        auto *bubble = new MessageBubble(message, own, showAvatar, m_showSenderNames, m_content);
        connect(bubble, &MessageBubble::retryRequested,
                this, &MessageView::retryRequested);
        connect(bubble, &MessageBubble::avatarActivated,
                this, &MessageView::avatarActivated);
        connect(bubble, &MessageBubble::attachmentRetryRequested,
                this, &MessageView::attachmentRetryRequested);
        connect(bubble, &MessageBubble::editRequested,
                this, &MessageView::editRequested);
        connect(bubble, &MessageBubble::deleteForEveryoneRequested,
                this, &MessageView::deleteForEveryoneRequested);
        connect(bubble, &MessageBubble::deleteForMeRequested,
                this, &MessageView::deleteForMeRequested);
        connect(bubble, &MessageBubble::imageActivated, this, &MessageView::imageActivated);
        connect(bubble, &MessageBubble::videoActivated, this, &MessageView::videoActivated);
        connect(bubble, &MessageBubble::fileActivated, this, &MessageView::fileActivated);
        m_layout->addWidget(bubble);

        // Появление последнего нового сообщения — мягкий fade.
        if (appended && m_animationsEnabled && i == messages.size() - 1) {
            auto *effect = new QGraphicsOpacityEffect(bubble);
            bubble->setGraphicsEffect(effect);

            auto *animation = new QPropertyAnimation(effect, "opacity", bubble);
            animation->setDuration(180);
            animation->setStartValue(0.0);
            animation->setEndValue(1.0);
            animation->setEasingCurve(QEasingCurve::OutCubic);

            connect(animation, &QPropertyAnimation::finished, bubble, [bubble, effect, animation]() {
                bubble->setGraphicsEffect(nullptr);
                effect->deleteLater();
                animation->deleteLater();
            });
            animation->start();
        }
        m_bubbles.append(bubble);

        previousTime = message.createdAt;
        previousSender = message.senderId;
    }

    m_layout->addStretch(1);
    m_lastKey = newLastKey;

    applyBubbleWidths();

    if (wasAtBottom || m_firstLoad) {
        scrollToBottom();
        if (appended)
            animateLastBubble();
    } else {
        // Сохраняем позицию относительно низа, чтобы подгрузка истории не «прыгала».
        const int newMaximum = verticalScrollBar()->maximum();
        verticalScrollBar()->setValue(qMax(0, newMaximum - (oldMaximum - oldValue)));
    }

    m_firstLoad = false;
}

void MessageView::clearContent()
{
    for (MessageBubble *bubble : qAsConst(m_bubbles)) {
        bubble->hide();
        bubble->deleteLater();
    }
    m_bubbles.clear();

    while (m_layout->count() > 0) {
        QLayoutItem *item = m_layout->takeAt(0);
        QWidget *widget = item->widget();
        // Плашку загрузки не удаляем: она переиспользуется при следующем чате.
        if (widget && widget != m_loading) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    if (m_loading)
        m_layout->addWidget(m_loading, 0, Qt::AlignHCenter);
}

void MessageView::addDaySeparator(qint64 unixMs)
{
    auto *label = new QLabel(format::dayLabel(unixMs), m_content);
    label->setObjectName(QStringLiteral("DayChip"));
    label->setAlignment(Qt::AlignCenter);
    m_layout->addWidget(label, 0, Qt::AlignHCenter);
}

void MessageView::applyBubbleWidths()
{
    const int width = int(viewport()->width() * 0.68);
    for (MessageBubble *bubble : qAsConst(m_bubbles))
        bubble->setMaxBubbleWidth(width);
}

void MessageView::animateLastBubble()
{
    if (m_bubbles.isEmpty())
        return;

    MessageBubble *bubble = m_bubbles.last();
    auto *effect = new QGraphicsOpacityEffect(bubble);
    bubble->setGraphicsEffect(effect);

    auto *animation = new QPropertyAnimation(effect, "opacity", bubble);
    animation->setDuration(160);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void MessageView::resizeEvent(QResizeEvent *event)
{
    QScrollArea::resizeEvent(event);
    applyBubbleWidths();
}

} // namespace ui
