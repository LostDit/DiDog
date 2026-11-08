#pragma once

#include <QFrame>
#include <QStringList>
#include <QTextEdit>

class QHBoxLayout;
class QLabel;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace ui {

class EmojiPicker;

// Многострочное поле ввода: Enter — отправить, Shift+Enter — перенос строки.
class ComposerEdit : public QTextEdit
{
    Q_OBJECT

public:
    explicit ComposerEdit(QWidget *parent = nullptr);

signals:
    void submitted();

protected:
    void keyPressEvent(QKeyEvent *event) override;
};

// Панель ввода: вложения с превью, эмодзи, текст и отправка.
class ComposerWidget : public QFrame
{
    Q_OBJECT

public:
    explicit ComposerWidget(QWidget *parent = nullptr);

    void setInputEnabled(bool enabled);
    void clear();

    void setReplyTarget(qint64 messageId, const QString &senderName, const QString &snippet);
    void clearReply();
    qint64 replyTargetId() const { return m_replyToId; }

signals:
    void sendRequested(const QString &text);
    void attachRequested(const QString &filePath);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onSendClicked();
    void onAttachClicked();
    void onTextChanged();
    void onEmojiSelected(const QString &symbol);

private:
    void adjustHeight();
    void addAttachment(const QString &path);
    void removeAttachment(const QString &path);
    void rebuildPreview();
    void updateSendState();
    bool hasContent() const;

    ComposerEdit *m_edit = nullptr;
    QToolButton *m_attachButton = nullptr;
    QToolButton *m_emojiButton = nullptr;
    QToolButton *m_sendButton = nullptr;
    EmojiPicker *m_emojiPicker = nullptr;

    QWidget *m_previewRow = nullptr;
    QHBoxLayout *m_previewLayout = nullptr;
    QStringList m_pendingFiles;

    QWidget *m_replyRow = nullptr;
    QLabel *m_replyLabel = nullptr;
    qint64 m_replyToId = 0;
};

} // namespace ui
