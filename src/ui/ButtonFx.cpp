#include "ui/ButtonFx.h"

#include "app/ThemeManager.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>

namespace ui {
namespace {

ButtonFx *g_instance = nullptr;

QColor accentColor()
{
    if (app::ThemeManager *theme = app::ThemeManager::instance()) {
        const QColor color = theme->color(QStringLiteral("accent"));
        if (color.isValid())
            return color;
    }
    return QColor(QStringLiteral("#3B7DF6"));
}

} // namespace

ButtonFx::ButtonFx(QApplication *app)
    : QObject(app)
{
    app->installEventFilter(this);
}

ButtonFx *ButtonFx::install(QApplication *app)
{
    if (!g_instance)
        g_instance = new ButtonFx(app);
    return g_instance;
}

void ButtonFx::setEnabled(bool enabled)
{
    if (!g_instance)
        return;

    g_instance->m_enabled = enabled;
    if (!enabled) {
        for (auto it = g_instance->m_states.begin(); it != g_instance->m_states.end(); ++it)
            g_instance->animate(it.key(), 0, 0);
    }
}

void ButtonFx::attach(QAbstractButton *button)
{
    if (m_states.contains(button))
        return;
    // Не трогаем кнопки с собственными эффектами (например, «Ответить»).
    if (button->graphicsEffect())
        return;

    State state;

    state.shadow = new QGraphicsDropShadowEffect(button);
    state.shadow->setOffset(0, 0);
    state.shadow->setBlurRadius(0);
    state.shadow->setColor(accentColor());
    button->setGraphicsEffect(state.shadow);

    state.blur = new QPropertyAnimation(state.shadow, "blurRadius", button);
    state.blur->setDuration(140);
    state.blur->setEasingCurve(QEasingCurve::OutCubic);

    state.offset = new QPropertyAnimation(state.shadow, "offset", button);
    state.offset->setDuration(110);
    state.offset->setEasingCurve(QEasingCurve::OutCubic);

    m_states.insert(button, state);
    button->installEventFilter(this);
}

void ButtonFx::animate(QAbstractButton *button, int blur, int offsetY)
{
    const auto it = m_states.constFind(button);
    if (it == m_states.constEnd())
        return;

    const State &state = it.value();

    if (!m_enabled || !button->isEnabled()) {
        state.blur->stop();
        state.shadow->setBlurRadius(0);
        state.offset->stop();
        state.shadow->setOffset(0, 0);
        return;
    }

    state.blur->stop();
    state.blur->setEndValue(blur);
    state.blur->start();

    state.offset->stop();
    state.offset->setEndValue(QPointF(0, offsetY));
    state.offset->start();
}

bool ButtonFx::eventFilter(QObject *watched, QEvent *event)
{
    // Прикрепляем эффекты к кнопкам после их создания (Polish).
    if (event->type() == QEvent::Polish) {
        if (auto *button = qobject_cast<QAbstractButton *>(watched))
            attach(button);
    }

    if (auto *button = qobject_cast<QAbstractButton *>(watched)) {
        switch (event->type()) {
        case QEvent::Enter: {
            State &state = m_states[button];
            state.hovered = true;
            animate(button, state.pressed ? 22 : 14, state.pressed ? 2 : 0);
            break;
        }
        case QEvent::Leave: {
            State &state = m_states[button];
            state.hovered = false;
            animate(button, state.pressed ? 16 : 0, state.pressed ? 2 : 0);
            break;
        }
        case QEvent::MouseButtonPress: {
            State &state = m_states[button];
            state.pressed = true;
            animate(button, 22, 2);
            break;
        }
        case QEvent::MouseButtonRelease: {
            State &state = m_states[button];
            state.pressed = false;
            animate(button, state.hovered ? 14 : 0, 0);
            break;
        }
        case QEvent::FocusIn:
            animate(button, 12, 0);
            break;
        case QEvent::FocusOut:
            animate(button, m_states.value(button).hovered ? 14 : 0, 0);
            break;
        case QEvent::EnabledChange:
            animate(button, 0, 0);
            break;
        default:
            break;
        }
    }

    return QObject::eventFilter(watched, event);
}

} // namespace ui
