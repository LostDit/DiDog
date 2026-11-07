#include "ui/MessageBubble.h"

#include "ui/AvatarWidget.h"
#include "ui/Formatting.h"

#include <QFileInfo>
#include <QFrame>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
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

    buildAttachment();

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
    const bool attachmentChanged = (m_message.localPath != message.localPath)
                                   || (m_message.attachment.id != message.attachment.id)
                                   || (m_message.kind != message.kind);

    m_message = message;

    if (textChanged && m_text) {
        m_text->setText(richText(message.body));
        m_text->setVisible(!message.body.isEmpty());
        adjustTextWidth();
    }

    if (attachmentChanged) {
        buildAttachment();
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

void MessageBubble::buildAttachment()
{
    if (m_message.kind == data::MessageKind::Text)
        return;

    if (m_attachment) {
        m_attachment->deleteLater();
        m_attachment = nullptr;
    }

    auto *layout = qobject_cast<QVBoxLayout *>(m_bubble->layout());
    if (!layout)
        return;

    const bool hasFile = !m_message.localPath.isEmpty() && QFileInfo::exists(m_message.localPath);

    auto *label = new QLabel(m_bubble);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMaximumWidth(340);

    if (m_message.kind == data::MessageKind::Image
        || m_message.kind == data::MessageKind::Gif) {
        label->setObjectName(QStringLiteral("AttachmentImage"));

        if (!hasFile) {
            label->setText(tr("Загрузка…"));
            label->setMinimumSize(160, 90);
        } else if (m_message.kind == data::MessageKind::Gif) {
            auto *movie = new QMovie(m_message.localPath, QByteArray(), label);
            if (movie->isValid()) {
                movie->jumpToFrame(0);
                const QSize original = movie->currentPixmap().size();
                if (original.isValid()) {
                    const QSize target = original.scaled(QSize(320, 240),
                                                         Qt::KeepAspectRatio);
                    movie->setScaledSize(target);
                    label->setFixedSize(target);
                }
                label->setMovie(movie);
                movie->start();
            } else {
                label->setText(tr("GIF недоступен"));
            }
        } else {
            QImageReader reader(m_message.localPath);
            reader.setAutoTransform(true);
            QSize size = reader.size();
            if (size.isValid()) {
                size.scale(QSize(320, 240), Qt::KeepAspectRatio);
                reader.setScaledSize(size);
            }
            const QImage image = reader.read();
            if (!image.isNull()) {
                label->setPixmap(QPixmap::fromImage(image));
                label->setFixedSize(image.size());
            } else {
                label->setText(tr("Не удалось открыть изображение"));
            }
        }
    } else if (m_message.kind == data::MessageKind::Video) {
        label->setObjectName(QStringLiteral("AttachmentFile"));
        const QString name = m_message.attachment.fileName.isEmpty()
                                 ? tr("Видео")
                                 : m_message.attachment.fileName;
        label->setText(QStringLiteral("▶  %1").arg(name));
    } else {
        label->setObjectName(QStringLiteral("AttachmentFile"));
        const QString name = m_message.attachment.fileName.isEmpty()
                                 ? QFileInfo(m_message.localPath).fileName()
                                 : m_message.attachment.fileName;
        qint64 size = m_message.attachment.byteSize;
        if (size <= 0 && hasFile)
            size = QFileInfo(m_message.localPath).size();
        label->setText(QStringLiteral("%1\n%2").arg(name, format::fileSize(size)));
    }

    layout->insertWidget(0, label, 0, Qt::AlignLeft);
    m_attachment = label;

    if (hasFile) {
        label->setCursor(Qt::PointingHandCursor);
        label->setToolTip(m_message.kind == data::MessageKind::Video
                              ? tr("Открыть видео")
                              : (m_message.kind == data::MessageKind::File
                                     ? tr("Сохранить файл")
                                     : tr("Открыть изображение")));
    }
}

void MessageBubble::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_attachment
        || m_message.localPath.isEmpty() || !m_attachment->geometry().contains(event->pos())) {
        QWidget::mousePressEvent(event);
        return;
    }

    switch (m_message.kind) {
    case data::MessageKind::Image:
    case data::MessageKind::Gif:
        emit imageActivated(m_message.localPath);
        break;
    case data::MessageKind::Video:
        emit videoActivated(m_message.localPath);
        break;
    case data::MessageKind::File:
        emit fileActivated(m_message.attachment.fileName, m_message.localPath);
        break;
    case data::MessageKind::Text:
        break;
    }

    event->accept();
}

void MessageBubble::updateStatus()
{
    if (!m_status)
        return;

    const bool uploading = m_message.uploadProgress >= 0 && m_message.uploadProgress < 100;
    if (uploading) {
        m_status->setText(QStringLiteral("%1%").arg(m_message.uploadProgress));
        m_status->setVisible(m_own);
        if (m_retry)
            m_retry->setVisible(false);
        return;
    }

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
