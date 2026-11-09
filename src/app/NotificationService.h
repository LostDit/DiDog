#pragma once

#include <QObject>
#include <QString>

class QWidget;

namespace app {

class SettingsStore;

// Всплывающие уведомления. Своей иконки в трее не имеет: показывает их через
// трей главного окна, чтобы в системе была ровно одна иконка MessengerApp.
class NotificationService : public QObject
{
    Q_OBJECT

public:
    explicit NotificationService(SettingsStore *settings, QObject *parent = nullptr);
    ~NotificationService() override;

    void setMainWindow(QWidget *window) { m_window = window; }

    // Показывает уведомление, если окно не активно или свёрнуто.
    void notify(const QString &title, const QString &body, qint64 chatId);
    void setUnreadCount(int count);

signals:
    void notificationRequested(const QString &title, const QString &body, qint64 chatId);
    void unreadChanged(int count);

private:
    SettingsStore *m_settings = nullptr;
    QWidget *m_window = nullptr;
    int m_unread = 0;
};

} // namespace app
