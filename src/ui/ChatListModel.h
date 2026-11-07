#pragma once

#include "data/Models.h"

#include <QAbstractListModel>
#include <QList>

namespace ui {

class ChatListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        SummaryRole = Qt::UserRole + 1
    };

    explicit ChatListModel(QObject *parent = nullptr);

    void setChats(const QList<data::ChatSummary> &chats);
    void setCurrentUserId(qint64 userId) { m_currentUserId = userId; }
    qint64 currentUserId() const { return m_currentUserId; }

    data::ChatSummary chatAt(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

private:
    QList<data::ChatSummary> m_chats;
    qint64 m_currentUserId = 0;
};

} // namespace ui
