#include "ui/ComposerWidget.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QTextDocument>
#include <QToolButton>

namespace ui {
namespace {

constexpr int kMinHeight = 42;
constexpr int kMaxHeight = 140;

} // namespace

ComposerEdit::ComposerEdit(QWidget *parent)
    : QTextEdit(parent)
{
    setObjectName(QStringLiteral("ComposerEdit"));
    setAcceptRichText(false);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setPlaceholderText(tr("Написать сообщение…"));
}

void ComposerEdit::keyPressEvent(QKeyEvent *event)
{
    const bool enter = (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter);
    const bool shift = event->modifiers() & Qt::ShiftModifier;

    if (enter && !shift) {
        emit submitted();
        event->accept();
        return;
    }

    QTextEdit::keyPressEvent(event);
}

ComposerWidget::ComposerWidget(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("Composer"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 12);
    layout->setSpacing(8);

    m_attachButton = new QToolButton(this);
    m_attachButton->setObjectName(QStringLiteral("IconButton"));
    m_attachButton->setIcon(QIcon(QStringLiteral(":/icons/paperclip.svg")));
    m_attachButton->setIconSize(QSize(20, 20));
    m_attachButton->setFixedSize(36, 36);
    m_attachButton->setCursor(Qt::PointingHandCursor);
    m_attachButton->setToolTip(tr("Прикрепить файл"));
    layout->addWidget(m_attachButton, 0, Qt::AlignBottom);

    m_edit = new ComposerEdit(this);
    m_edit->setAccessibleName(tr("Поле ввода сообщения"));
    m_edit->setFixedHeight(kMinHeight);
    layout->addWidget(m_edit, 1);

    m_sendButton = new QToolButton(this);
    m_sendButton->setObjectName(QStringLiteral("SendButton"));
    m_sendButton->setIcon(QIcon(QStringLiteral(":/icons/send.svg")));
    m_sendButton->setIconSize(QSize(20, 20));
    m_sendButton->setFixedSize(36, 36);
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setToolTip(tr("Отправить (Enter)"));
    m_sendButton->setEnabled(false);
    layout->addWidget(m_sendButton, 0, Qt::AlignBottom);

    connect(m_sendButton, &QToolButton::clicked, this, &ComposerWidget::onSendClicked);
    connect(m_attachButton, &QToolButton::clicked, this, &ComposerWidget::onAttachClicked);
    connect(m_edit, &ComposerEdit::submitted, this, &ComposerWidget::onSendClicked);
    connect(m_edit, &QTextEdit::textChanged, this, &ComposerWidget::onTextChanged);
}

void ComposerWidget::setInputEnabled(bool enabled)
{
    m_edit->setEnabled(enabled);
    m_attachButton->setEnabled(enabled);
    m_sendButton->setEnabled(enabled && !m_edit->toPlainText().trimmed().isEmpty());
}

void ComposerWidget::clear()
{
    m_edit->clear();
}

void ComposerWidget::onAttachClicked()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Прикрепить файл"), QString(),
        tr("Все файлы (*.*);;Изображения (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;"
           "Видео (*.mp4 *.mov *.mkv *.webm *.avi)"));

    if (!path.isEmpty())
        emit attachRequested(path);
}

void ComposerWidget::onSendClicked()
{
    const QString text = m_edit->toPlainText().trimmed();
    if (text.isEmpty())
        return;

    emit sendRequested(text);
    m_edit->clear();
    m_edit->setFocus();
}

void ComposerWidget::onTextChanged()
{
    const bool hasText = !m_edit->toPlainText().trimmed().isEmpty();
    m_sendButton->setEnabled(hasText && m_edit->isEnabled());
    adjustHeight();
}

void ComposerWidget::adjustHeight()
{
    const qreal documentHeight = m_edit->document()->size().height();
    const int height = qBound(kMinHeight, int(documentHeight) + 16, kMaxHeight);
    if (height != m_edit->height())
        m_edit->setFixedHeight(height);
}

} // namespace ui
