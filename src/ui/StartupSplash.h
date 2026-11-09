#pragma once

#include <QWidget>

class QLabel;
class QTimer;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

namespace ui {

// Стартовая заставка: логотип, название и вращающийся индикатор.
// Плавно появляется, держится и растворяется, открывая окно входа/чат.
class StartupSplash : public QWidget
{
    Q_OBJECT

public:
    explicit StartupSplash(QWidget *parent = nullptr);

    void start();
    void finish();

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    void fadeOut();

    QWidget *m_card = nullptr;
    QLabel *m_logo = nullptr;
    QLabel *m_status = nullptr;
    QGraphicsOpacityEffect *m_cardOpacity = nullptr;
    QPropertyAnimation *m_fade = nullptr;
    QPropertyAnimation *m_pulse = nullptr;
    bool m_finishing = false;
};

} // namespace ui
