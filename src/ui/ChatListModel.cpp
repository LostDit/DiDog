#include "ui/ChatListModel.h"

namespace ui {

ChatListModel::ChatListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void ChatListModel::setChats(const QList<data::ChatSummary> &chats)
{
    beginResetModel();
    m_chats = chats;
    endResetModel();
}

data::ChatSummary ChatListModel::chatAt(int row) const
{
    if (row < 0 || row >= m_chats.size())
        return data::ChatSummary();
    return m_chats.at(row);
}

int ChatListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_chats.size();
}

QVariant ChatListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_chats.size())
        return QVariant();

    const data::ChatSummary &summary = m_chats.at(index.row());

    if (role == SummaryRole)
        return QVariant::fromValue(summary);

    if (role == Qt::DisplayRole)
        return summary.peerDisplayName;

    return QVariant();
}

} // namespace ui
