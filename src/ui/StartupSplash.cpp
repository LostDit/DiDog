#include "ui/StartupSplash.h"

#include "app/ThemeManager.h"

#include <QApplication>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace ui {
namespace {

// Вращающаяся дуга под логотипом.
class SplashSpinner : public QWidget
{
public:
    explicit SplashSpinner(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(28, 28);
        m_timer.setInterval(40);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this]() {
            m_angle = (m_angle + 30) % 360;
            update();
        });
        m_timer.start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QColor color(QStringLiteral("#5B8CFF"));
        if (app::ThemeManager *theme = app::ThemeManager::instance()) {
            const QColor accent = theme->color(QStringLiteral("accent"));
            if (accent.isValid())
                color = accent;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(rect().adjusted(4, 4, -4, -4), m_angle * 16, 120 * 16);
    }

private:
    QTimer m_timer;
    int m_angle = 0;
};

} // namespace

StartupSplash::StartupSplash(QWidget *parent)
    : QWidget(parent, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setObjectName(QStringLiteral("SplashRoot"));
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedSize(400, 320);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(20, 20, 20, 20);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("SplashCard"));
    m_card = card;
    outer->addWidget(card);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(28, 30, 28, 26);
    layout->setSpacing(10);
    layout->addStretch(1);

    m_logo = new QLabel(card);
    m_logo->setPixmap(QIcon(QStringLiteral(":/icons/logo.svg")).pixmap(76, 76));
    m_logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_logo);

    auto *title = new QLabel(QStringLiteral("MessengerApp"), card);
    title->setObjectName(QStringLiteral("SplashTitle"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    m_status = new QLabel(tr("Загрузка…"), card);
    m_status->setObjectName(QStringLiteral("SplashStatus"));
    m_status->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_status);

    auto *spinnerRow = new QHBoxLayout;
    spinnerRow->addStretch(1);
    spinnerRow->addWidget(new SplashSpinner(card));
    spinnerRow->addStretch(1);
    layout->addLayout(spinnerRow);

    layout->addStretch(1);

    m_pulse = nullptr;

    // Появление и исчезновение карточки.
    m_cardOpacity = new QGraphicsOpacityEffect(card);
    m_cardOpacity->setOpacity(0.0);
    card->setGraphicsEffect(m_cardOpacity);

    m_fade = new QPropertyAnimation(m_cardOpacity, "opacity", this);
    m_fade->setDuration(280);
    m_fade->setEasingCurve(QEasingCurve::OutCubic);

    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        move(available.center() - rect().center());
    }
}

void StartupSplash::start()
{
    show();
    raise();


    m_fade->stop();
    m_fade->setStartValue(0.0);
    m_fade->setEndValue(1.0);
    m_fade->start();

    QTimer::singleShot(1400, this, &StartupSplash::finish);
}

void StartupSplash::finish()
{
    if (m_finishing)
        return;
    m_finishing = true;
    fadeOut();
}

void StartupSplash::fadeOut()
{

    m_fade->stop();
    m_fade->setStartValue(m_cardOpacity->opacity());
    m_fade->setEndValue(0.0);
    m_fade->start();

    QObject::connect(m_fade, &QPropertyAnimation::finished, this, [this]() {
        emit finished();
        close();
    }, Qt::UniqueConnection);
}

void StartupSplash::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    finish();
    QWidget::mousePressEvent(event);
}

} // namespace ui
