#include "ui/VideoPlayerDialog.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSlider>
#include <QTime>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

namespace ui {
namespace {

QString formatTime(qint64 ms)
{
    const QTime time = QTime(0, 0).addMSecs(static_cast<int>(qMax<qint64>(0, ms)));
    return time.toString(QStringLiteral("mm:ss"));
}

} // namespace

VideoPlayerDialog::VideoPlayerDialog(const QString &filePath, QWidget *parent)
    : QDialog(parent)
    , m_filePath(filePath)
{
    setObjectName(QStringLiteral("VideoViewer"));
    setWindowTitle(QFileInfo(filePath).fileName());
    setModal(true);
    resize(900, 640);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    m_video = new QVideoWidget(this);
    m_video->setObjectName(QStringLiteral("VideoSurface"));
    m_video->setMinimumHeight(360);
    layout->addWidget(m_video, 1);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("ErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(10);

    m_playButton = new QToolButton(this);
    m_playButton->setObjectName(QStringLiteral("IconButton"));
    m_playButton->setText(QStringLiteral("▶"));
    m_playButton->setFixedSize(38, 38);
    m_playButton->setCursor(Qt::PointingHandCursor);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 0);
    m_slider->setCursor(Qt::PointingHandCursor);

    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), this);
    m_timeLabel->setObjectName(QStringLiteral("ChatStatus"));

    controls->addWidget(m_playButton);
    controls->addWidget(m_slider, 1);
    controls->addWidget(m_timeLabel);
    layout->addLayout(controls);

    auto *fallback = new QPushButton(tr("Открыть внешним плеером"), this);
    fallback->setObjectName(QStringLiteral("GhostButton"));
    fallback->setCursor(Qt::PointingHandCursor);
    connect(fallback, &QPushButton::clicked, this, &VideoPlayerDialog::openExternally);
    layout->addWidget(fallback, 0, Qt::AlignRight);

    m_player = new QMediaPlayer(this);
    m_player->setVideoOutput(m_video);
    m_player->setMedia(QMediaContent(QUrl::fromLocalFile(filePath)));

    connect(m_playButton, &QToolButton::clicked, this, &VideoPlayerDialog::togglePlayback);
    connect(m_slider, &QSlider::sliderMoved, m_player, &QMediaPlayer::setPosition);
    connect(m_player, &QMediaPlayer::positionChanged, this, &VideoPlayerDialog::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &VideoPlayerDialog::onDurationChanged);
    connect(m_player, &QMediaPlayer::stateChanged, this, &VideoPlayerDialog::onPlayerStateChanged);
    connect(m_player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error),
            this, &VideoPlayerDialog::onError);

    m_player->play();
}

VideoPlayerDialog::~VideoPlayerDialog()
{
    // Плеер удаляем до видеовиджета: он держит указатель на поверхность вывода.
    if (m_player) {
        m_player->stop();
        m_player->setMedia(QMediaContent());
        delete m_player;
        m_player = nullptr;
    }
}

void VideoPlayerDialog::togglePlayback()
{
    if (m_player->state() == QMediaPlayer::PlayingState)
        m_player->pause();
    else
        m_player->play();
}

void VideoPlayerDialog::onPositionChanged(qint64 position)
{
    if (!m_slider->isSliderDown())
        m_slider->setValue(static_cast<int>(position));

    m_timeLabel->setText(QStringLiteral("%1 / %2")
                             .arg(formatTime(position),
                                  formatTime(m_player->duration())));
}

void VideoPlayerDialog::onDurationChanged(qint64 duration)
{
    m_slider->setRange(0, static_cast<int>(qMax<qint64>(0, duration)));
}

void VideoPlayerDialog::onPlayerStateChanged(int state)
{
    m_playButton->setText(state == QMediaPlayer::PlayingState ? QStringLiteral("❚❚")
                                                              : QStringLiteral("▶"));
}

void VideoPlayerDialog::onError()
{
    m_hasError = true;
    m_errorText = m_player->errorString();

    m_errorLabel->setText(tr("Не удалось воспроизвести видео: %1").arg(m_errorText));
    m_errorLabel->show();
}

void VideoPlayerDialog::openExternally()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_filePath));
}

} // namespace ui
