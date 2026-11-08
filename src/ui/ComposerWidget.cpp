#include "ui/ComposerWidget.h"

#include "ui/EmojiPicker.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QTextDocument>
#include <QUrl>
#include <QToolButton>
#include <QVBoxLayout>

namespace ui {
namespace {

constexpr int kMinHeight = 42;
constexpr int kMaxHeight = 140;

const QStringList kFileFilters = {
    QStringLiteral("Все файлы (*.*)"),
    QStringLiteral("Изображения (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"),
    QStringLiteral("Видео (*.mp4 *.mov *.mkv *.webm *.avi)"),
};

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
    setAcceptDrops(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 8, 12, 12);
    root->setSpacing(6);

    // Превью выбранных вложений (скрыто, пока ничего не выбрано).
    m_previewRow = new QWidget(this);
    m_previewRow->setObjectName(QStringLiteral("ComposerPreview"));
    m_previewLayout = new QHBoxLayout(m_previewRow);
    m_previewLayout->setContentsMargins(0, 0, 0, 0);
    m_previewLayout->setSpacing(6);
    m_previewLayout->addStretch(1);
    m_previewRow->hide();
    root->addWidget(m_previewRow);

    auto *inputRow = new QHBoxLayout;
    inputRow->setSpacing(8);

    m_attachButton = new QToolButton(this);
    m_attachButton->setObjectName(QStringLiteral("IconButton"));
    m_attachButton->setIcon(QIcon(QStringLiteral(":/icons/paperclip.svg")));
    m_attachButton->setIconSize(QSize(20, 20));
    m_attachButton->setFixedSize(36, 36);
    m_attachButton->setCursor(Qt::PointingHandCursor);
    m_attachButton->setToolTip(tr("Прикрепить файл"));
    inputRow->addWidget(m_attachButton, 0, Qt::AlignBottom);

    m_emojiButton = new QToolButton(this);
    m_emojiButton->setObjectName(QStringLiteral("IconButton"));
    m_emojiButton->setText(QStringLiteral("🙂"));
    m_emojiButton->setToolTip(tr("Эмодзи"));
    m_emojiButton->setFixedSize(36, 36);
    m_emojiButton->setCursor(Qt::PointingHandCursor);
    inputRow->addWidget(m_emojiButton, 0, Qt::AlignBottom);

    m_edit = new ComposerEdit(this);
    m_edit->setAccessibleName(tr("Поле ввода сообщения"));
    m_edit->setFixedHeight(kMinHeight);
    inputRow->addWidget(m_edit, 1);

    m_sendButton = new QToolButton(this);
    m_sendButton->setObjectName(QStringLiteral("SendButton"));
    m_sendButton->setIcon(QIcon(QStringLiteral(":/icons/send.svg")));
    m_sendButton->setIconSize(QSize(20, 20));
    m_sendButton->setFixedSize(36, 36);
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setToolTip(tr("Отправить (Enter)"));
    m_sendButton->setEnabled(false);
    inputRow->addWidget(m_sendButton, 0, Qt::AlignBottom);

    root->addLayout(inputRow);

    m_emojiPicker = new EmojiPicker(this);
    connect(m_emojiPicker, &EmojiPicker::emojiSelected,
            this, &ComposerWidget::onEmojiSelected);

    connect(m_sendButton, &QToolButton::clicked, this, &ComposerWidget::onSendClicked);
    connect(m_attachButton, &QToolButton::clicked, this, &ComposerWidget::onAttachClicked);
    connect(m_emojiButton, &QToolButton::clicked, this, [this]() {
        m_emojiPicker->adjustSize();
        m_emojiPicker->popup(m_emojiButton->mapToGlobal(QPoint(0, -m_emojiPicker->sizeHint().height() - 6)));
    });
    connect(m_edit, &ComposerEdit::submitted, this, &ComposerWidget::onSendClicked);
    connect(m_edit, &QTextEdit::textChanged, this, &ComposerWidget::onTextChanged);
}

void ComposerWidget::setInputEnabled(bool enabled)
{
    m_edit->setEnabled(enabled);
    m_attachButton->setEnabled(enabled);
    m_emojiButton->setEnabled(enabled);
    updateSendState();
}

void ComposerWidget::clear()
{
    m_edit->clear();
    m_pendingFiles.clear();
    rebuildPreview();
    updateSendState();
}

void ComposerWidget::onAttachClicked()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Прикрепить файл"), QString(), kFileFilters.join(QStringLiteral(";;")));

    if (!path.isEmpty())
        addAttachment(path);
}

void ComposerWidget::onEmojiSelected(const QString &symbol)
{
    if (symbol.isEmpty() || !m_edit->isEnabled())
        return;

    m_edit->textCursor().insertText(symbol);
    m_edit->setFocus();
}

void ComposerWidget::onSendClicked()
{
    const QString text = m_edit->toPlainText().trimmed();
    const QStringList files = m_pendingFiles;

    if (text.isEmpty() && files.isEmpty())
        return;

    if (!text.isEmpty())
        emit sendRequested(text);

    for (const QString &path : files)
        emit attachRequested(path);

    m_edit->clear();
    m_pendingFiles.clear();
    rebuildPreview();
    updateSendState();
    m_edit->setFocus();
}

void ComposerWidget::onTextChanged()
{
    updateSendState();
    adjustHeight();
}

void ComposerWidget::updateSendState()
{
    m_sendButton->setEnabled(m_edit->isEnabled() && hasContent());
}

bool ComposerWidget::hasContent() const
{
    return !m_edit->toPlainText().trimmed().isEmpty() || !m_pendingFiles.isEmpty();
}

void ComposerWidget::addAttachment(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path) || m_pendingFiles.contains(path))
        return;

    m_pendingFiles.append(path);
    rebuildPreview();
    updateSendState();
}

void ComposerWidget::removeAttachment(const QString &path)
{
    m_pendingFiles.removeAll(path);
    rebuildPreview();
    updateSendState();
}

void ComposerWidget::rebuildPreview()
{
    while (QLayoutItem *item = m_previewLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    for (const QString &path : m_pendingFiles) {
        const QFileInfo info(path);

        auto *chip = new QFrame(m_previewRow);
        chip->setObjectName(QStringLiteral("AttachmentChip"));
        auto *chipLayout = new QHBoxLayout(chip);
        chipLayout->setContentsMargins(8, 4, 4, 4);
        chipLayout->setSpacing(6);

        auto *name = new QLabel(info.fileName(), chip);
        name->setObjectName(QStringLiteral("AttachmentChipName"));
        name->setTextFormat(Qt::PlainText);
        name->setMaximumWidth(220);
        chipLayout->addWidget(name);

        auto *remove = new QToolButton(chip);
        remove->setText(QStringLiteral("×"));
        remove->setObjectName(QStringLiteral("AttachmentChipRemove"));
        remove->setCursor(Qt::PointingHandCursor);
        remove->setFixedSize(20, 20);
        chipLayout->addWidget(remove);

        connect(remove, &QToolButton::clicked, this, [this, path]() {
            removeAttachment(path);
        });

        m_previewLayout->addWidget(chip);
    }

    m_previewLayout->addStretch(1);
    m_previewRow->setVisible(!m_pendingFiles.isEmpty());

    if (m_pendingFiles.isEmpty())
        adjustHeight();
}

void ComposerWidget::adjustHeight()
{
    const qreal documentHeight = m_edit->document()->size().height();
    const int height = qBound(kMinHeight, int(documentHeight) + 16, kMaxHeight);
    if (height != m_edit->height())
        m_edit->setFixedHeight(height);
}

void ComposerWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void ComposerWidget::dropEvent(QDropEvent *event)
{
    bool added = false;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        addAttachment(url.toLocalFile());
        added = true;
    }

    if (added)
        event->acceptProposedAction();
}

} // namespace ui
