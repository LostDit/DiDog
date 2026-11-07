#pragma once

#include <QObject>
#include <QString>

class QMenu;
class QSystemTrayIcon;
class QWidget;

namespace app {

class SettingsStore;

// Иконка в системном трее, счётчик непрочитанных и всплывающие уведомления.
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
    void showRequested();
    void openChatRequested(qint64 chatId);
    void quitRequested();

private:
    void updateTooltip();

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    SettingsStore *m_settings = nullptr;
    QWidget *m_window = nullptr;
    int m_unread = 0;
    qint64 m_lastChatId = 0;
    bool m_available = false;
};

} // namespace app
