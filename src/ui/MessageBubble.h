#pragma once

#include "data/Models.h"

#include <QWidget>

class QFrame;
class QLabel;
class QPushButton;

namespace ui {

// Один пузырь сообщения: текст, время и статус доставки.
class MessageBubble : public QWidget
{
    Q_OBJECT

public:
    MessageBubble(const data::Message &message, bool own, bool showAvatar,
                  QWidget *parent = nullptr);

    const data::Message &message() const { return m_message; }
    QString clientMsgId() const { return m_message.clientMsgId; }
    qint64 messageId() const { return m_message.id; }

    void setMaxBubbleWidth(int width);
    void updateMessage(const data::Message &message);

signals:
    void retryRequested(const QString &clientMsgId);

private:
    void rebuild();
    void updateStatus();
    void adjustTextWidth();

    data::Message m_message;
    bool m_own = false;
    bool m_showAvatar = false;
    int m_maxTextWidth = 420;

    QFrame *m_bubble = nullptr;
    QLabel *m_text = nullptr;
    QLabel *m_time = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_retry = nullptr;
};

} // namespace ui
