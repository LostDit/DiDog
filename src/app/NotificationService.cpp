#include "app/NotificationService.h"

#include "app/SettingsStore.h"

#include <QWidget>

namespace app {

NotificationService::NotificationService(SettingsStore *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

NotificationService::~NotificationService() = default;

void NotificationService::notify(const QString &title, const QString &body, qint64 chatId)
{
    if (m_settings && !m_settings->notificationsEnabled())
        return;

    if (m_window && m_window->isActiveWindow() && !m_window->isMinimized())
        return;

    emit notificationRequested(title, body, chatId);
}

void NotificationService::setUnreadCount(int count)
{
    m_unread = qMax(0, count);

    if (m_window) {
        const QString base = QStringLiteral("MessengerApp");
        m_window->setWindowTitle(m_unread > 0
                                     ? QStringLiteral("(%1) %2").arg(m_unread).arg(base)
                                     : base);
    }

    emit unreadChanged(m_unread);
}

} // namespace app
