#pragma once

#include "data/Models.h"

#include <QList>
#include <QScrollArea>

class QLabel;
class QVBoxLayout;
class QWidget;

namespace ui {

class MessageBubble;

// Лента сообщений: пузыри, разделители дат, автопрокрутка и подгрузка истории.
class MessageView : public QScrollArea
{
    Q_OBJECT

public:
    explicit MessageView(QWidget *parent = nullptr);

    void setCurrentUserId(qint64 userId) { m_currentUserId = userId; }
    void setHasMore(bool hasMore) { m_hasMore = hasMore; }

    // Полная перестройка ленты. При добавлении нового сообщения в конец —
    // анимирует его появление, если пользователь у нижней границы.
    void setMessages(const QList<data::Message> &messages);

    // Показывает заглушку на время загрузки истории.
    void setLoading(bool loading);

    void scrollToBottom();
    bool isAtBottom() const;

    // Прокрутить к сообщению, если оно есть в ленте.
    bool scrollToMessage(qint64 messageId);

signals:
    void loadOlderRequested();
    void retryRequested(const QString &clientMsgId);
    void attachmentRetryRequested(qint64 attachmentId);
    void editRequested(qint64 messageId);
    void deleteForEveryoneRequested(qint64 messageId);
    void deleteForMeRequested(qint64 messageId);
    void imageActivated(const QString &filePath);
    void videoActivated(const QString &filePath);
    void fileActivated(const QString &fileName, const QString &filePath);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void clearContent();
    void addDaySeparator(qint64 unixMs);
    void applyBubbleWidths();
    void animateLastBubble();

    QWidget *m_content = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QLabel *m_loading = nullptr;
    QList<MessageBubble *> m_bubbles;
    qint64 m_currentUserId = 0;
    bool m_hasMore = false;
    QString m_lastKey;
    bool m_firstLoad = true;
};

} // namespace ui
