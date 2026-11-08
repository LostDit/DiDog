#pragma once

#include "data/Models.h"

#include <QDialog>
#include <QHash>
#include <QList>

class QLineEdit;
class QListWidget;

namespace app {
class ChatService;
}

namespace ui {

// Панель «Участники» группового чата в духе макета: аватары, статусы, роли,
// поиск по именам и действия владельца/администратора.
class ParticipantsDialog : public QDialog
{
    Q_OBJECT

public:
    ParticipantsDialog(app::ChatService *chats, qint64 chatId, qint64 currentUserId,
                       QWidget *parent = nullptr);

private slots:
    void onMembersLoaded(qint64 chatId, const QList<data::ChatMember> &members);
    void onMembersChanged(qint64 chatId);
    void onSearchChanged(const QString &text);
    void onAddClicked();
    void onLeaveClicked();

private:
    void rebuild();
    bool canManage() const;

    app::ChatService *m_chats = nullptr;
    qint64 m_chatId = 0;
    qint64 m_currentUserId = 0;
    QString m_myRole;
    QList<data::ChatMember> m_members;

    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QWidget *m_addButton = nullptr;
};

} // namespace ui
