#pragma once

#include "data/Models.h"

#include <QDialog>
#include <QList>

namespace app {
class ChatService;
}

namespace ui {

// Карточка профиля: баннер, аватар, имя, @логин, статус и кнопка «Написать».
class ProfileCardDialog : public QDialog
{
    Q_OBJECT

public:
    ProfileCardDialog(app::ChatService *chats, qint64 chatId, qint64 userId,
                      qint64 currentUserId, QWidget *parent = nullptr);

signals:
    void callRequested(qint64 chatId);

private slots:
    void onMembersLoaded(qint64 chatId, const QList<data::ChatMember> &members);

private:
    void buildUi(const QString &displayName, const QString &username, int hue, bool online,
                 bool known);

    app::ChatService *m_chats = nullptr;
    qint64 m_chatId = 0;
    qint64 m_userId = 0;
    qint64 m_currentUserId = 0;

    QString m_displayName;
    QString m_username;
    int m_hue = 210;
    bool m_online = false;
    bool m_loaded = false;

    QWidget *m_content = nullptr;
};

} // namespace ui
