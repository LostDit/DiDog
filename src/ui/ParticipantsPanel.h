#pragma once

#include "data/Models.h"

#include <QList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace app {
class ChatService;
}

namespace ui {

// Правая панель участников группового чата (в духе Discord):
// аватары, статусы, роли, поиск и действия владельца.
class ParticipantsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ParticipantsPanel(app::ChatService *chats, QWidget *parent = nullptr);

    void setChat(qint64 chatId, qint64 currentUserId);
    void refresh();

signals:
    void closeRequested();
    void profileRequested(qint64 userId);

private slots:
    void onMembersLoaded(qint64 chatId, const QList<data::ChatMember> &members);
    void onMembersChanged(qint64 chatId);
    void onSearchChanged(const QString &text);
    void onAddClicked();
    void onLeaveClicked();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();
    bool canManage() const;

    app::ChatService *m_chats = nullptr;
    qint64 m_chatId = 0;
    qint64 m_currentUserId = 0;
    QString m_myRole;
    QList<data::ChatMember> m_members;

    QLabel *m_count = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_addButton = nullptr;
};

} // namespace ui
