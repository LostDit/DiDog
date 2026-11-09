#include "ui/PanScrollArea.h"

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QScrollBar>

namespace ui {

PanScrollArea::PanScrollArea(QWidget *parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->installEventFilter(this);
    viewport()->setCursor(Qt::ArrowCursor);
}

bool PanScrollArea::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != viewport())
        return QScrollArea::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            m_pressed = true;
            m_dragging = false;
            m_pressPosition = mouse->pos();
        }
        break;
    }
    case QEvent::MouseMove: {
        if (!m_pressed)
            break;

        auto *mouse = static_cast<QMouseEvent *>(event);
        const QPoint delta = mouse->pos() - m_pressPosition;

        if (!m_dragging && delta.manhattanLength() > 6) {
            m_dragging = true;
            viewport()->setCursor(Qt::ClosedHandCursor);
        }

        if (m_dragging) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            m_pressPosition = mouse->pos();
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        const bool wasDragging = m_dragging;
        m_pressed = false;
        m_dragging = false;
        viewport()->setCursor(Qt::ArrowCursor);
        if (wasDragging)
            return true;
        break;
    }
    case QEvent::Leave:
        m_pressed = false;
        m_dragging = false;
        viewport()->setCursor(Qt::ArrowCursor);
        break;
    default:
        break;
    }

    return QScrollArea::eventFilter(watched, event);
}

} // namespace ui
