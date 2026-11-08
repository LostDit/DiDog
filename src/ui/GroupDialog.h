#pragma once

#include "data/Models.h"

#include <QDialog>
#include <QList>
#include <QSet>
#include <QString>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace app {
class ChatService;
}

namespace ui {

// Создание группы или добавление участников: поиск людей и выбор галочками.
class GroupDialog : public QDialog
{
    Q_OBJECT

public:
    // chatId <= 0 — создание группы; иначе добавление участников в существующую.
    GroupDialog(app::ChatService *chats, qint64 chatId, const QSet<qint64> &exclude,
                QWidget *parent = nullptr);

    QString groupTitle() const;
    QList<qint64> selectedIds() const;

private slots:
    void onUsersFound(const QString &query, const QList<data::User> &users);
    void onItemChanged(QListWidgetItem *item);

private:
    void rebuild();
    void updateSubmit();

    app::ChatService *m_chats = nullptr;
    qint64 m_chatId = 0;
    QSet<qint64> m_exclude;
    QSet<qint64> m_selected;
    QList<data::User> m_users;

    QLineEdit *m_title = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_submit = nullptr;
};

} // namespace ui
