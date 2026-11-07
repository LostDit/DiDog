#pragma once

#include <QStyledItemDelegate>

namespace ui {

// Рисует строку списка чатов: аватар, имя, превью, время, непрочитанные.
class ChatItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit ChatItemDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

} // namespace ui
