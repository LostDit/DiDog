#pragma once

#include "data/Models.h"

#include <QWidget>

class QFrame;
class QLabel;
class QPushButton;
class QToolButton;
class QGraphicsOpacityEffect;

namespace ui {

class AvatarWidget;

// Один пузырь сообщения: текст, время и статус доставки.
class MessageBubble : public QWidget
{
    Q_OBJECT

public:
    MessageBubble(const data::Message &message, bool own, bool showAvatar,
                  bool showSenderName = false, QWidget *parent = nullptr);

    const data::Message &message() const { return m_message; }
    QString clientMsgId() const { return m_message.clientMsgId; }
    qint64 messageId() const { return m_message.id; }

    void setMaxBubbleWidth(int width);
    void updateMessage(const data::Message &message);

signals:
    void retryRequested(const QString &clientMsgId);
    void attachmentRetryRequested(qint64 attachmentId);
    void editRequested(qint64 messageId);
    void deleteForEveryoneRequested(qint64 messageId);
    void deleteForMeRequested(qint64 messageId);
    void avatarActivated(qint64 userId);
    void imageActivated(const QString &filePath);
    void videoActivated(const QString &filePath);
    void fileActivated(const QString &fileName, const QString &filePath);
    void replyRequested(qint64 messageId, const QString &senderName, const QString &body);
    void reactionToggled(qint64 messageId, const QString &emoji, bool on);
    void forwardRequested(qint64 messageId);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void rebuild();
    void updateStatus();
    void adjustTextWidth();
    void buildAttachment();
    void showContextMenu(const QPoint &position);

    data::Message m_message;
    bool m_own = false;
    bool m_showAvatar = false;
    bool m_showSenderName = false;
    int m_maxTextWidth = 420;

    AvatarWidget *m_avatar = nullptr;

    QFrame *m_bubble = nullptr;
    QLabel *m_text = nullptr;
    QLabel *m_attachment = nullptr;
    QString m_builtPath;
    QLabel *m_time = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_retry = nullptr;
    QToolButton *m_replyButton = nullptr;
    QToolButton *m_reactButton = nullptr;
    QGraphicsOpacityEffect *m_reactEffect = nullptr;
    QGraphicsOpacityEffect *m_replyEffect = nullptr;
};

} // namespace ui
