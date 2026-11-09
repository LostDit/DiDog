#pragma once

#include <QPoint>
#include <QScrollArea>

class QMouseEvent;

namespace ui {

// Прокрутка содержимого перетаскиванием левой кнопкой мыши (как на тачпаде).
// Короткий клик проходит к виджетам, перетаскивание — прокручивает.
class PanScrollArea : public QScrollArea
{
    Q_OBJECT

public:
    explicit PanScrollArea(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPoint m_pressPosition;
    bool m_pressed = false;
    bool m_dragging = false;
};

} // namespace ui
