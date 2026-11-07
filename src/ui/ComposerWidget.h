#pragma once

#include <QFrame>
#include <QTextEdit>

class QToolButton;

namespace ui {

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

// Панель ввода сообщения: прикрепление, текст, отправка.
class ComposerWidget : public QFrame
{
    Q_OBJECT

public:
    explicit ComposerWidget(QWidget *parent = nullptr);

    void setInputEnabled(bool enabled);
    void clear();

signals:
    void sendRequested(const QString &text);

private slots:
    void onSendClicked();
    void onTextChanged();

private:
    void adjustHeight();

    ComposerEdit *m_edit = nullptr;
    QToolButton *m_attachButton = nullptr;
    QToolButton *m_sendButton = nullptr;
};

} // namespace ui
