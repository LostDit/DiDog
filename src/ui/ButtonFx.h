#pragma once

#include <QHash>
#include <QObject>

class QAbstractButton;
class QApplication;
class QGraphicsDropShadowEffect;
class QPropertyAnimation;

namespace ui {

// Глобальные анимации кнопок: свечение при наведении, «нажатие» и фокус-кольцо.
// Ставится один раз на всё приложение; уважает настройку «Анимации интерфейса».
class ButtonFx : public QObject
{
    Q_OBJECT

public:
    static ButtonFx *install(QApplication *app);
    static void setEnabled(bool enabled);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit ButtonFx(QApplication *app);
    void attach(QAbstractButton *button);
    void animate(QAbstractButton *button, int blur, int offsetY);

    struct State {
        QGraphicsDropShadowEffect *shadow = nullptr;
        QPropertyAnimation *blur = nullptr;
        QPropertyAnimation *offset = nullptr;
        bool hovered = false;
        bool pressed = false;
    };

    QHash<QAbstractButton *, State> m_states;
    bool m_enabled = true;
};

} // namespace ui
