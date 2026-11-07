#pragma once

#include <QDialog>

class QLabel;
class QMediaPlayer;
class QSlider;
class QToolButton;
class QVideoWidget;

namespace ui {

// Модальный видеоплеер: play/pause, перемотка, громкость, время.
class VideoPlayerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VideoPlayerDialog(const QString &filePath, QWidget *parent = nullptr);
    ~VideoPlayerDialog() override;

    bool hasError() const { return m_hasError; }
    QString errorText() const { return m_errorText; }

private slots:
    void togglePlayback();
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onPlayerStateChanged(int state);
    void onError();

private:
    void openExternally();

    QString m_filePath;
    QMediaPlayer *m_player = nullptr;
    QVideoWidget *m_video = nullptr;
    QSlider *m_slider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QToolButton *m_playButton = nullptr;
    bool m_hasError = false;
    QString m_errorText;
};

} // namespace ui
