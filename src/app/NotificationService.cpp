#include "app/NotificationService.h"

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QWidget>

namespace app {

NotificationService::NotificationService(QObject *parent)
    : QObject(parent)
{
    m_available = QSystemTrayIcon::isSystemTrayAvailable();

    m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/logo.svg")), this);
    m_tray->setToolTip(QStringLiteral("MessengerApp"));

    m_menu = new QMenu;
    QAction *showAction = m_menu->addAction(tr("Открыть MessengerApp"));
    m_menu->addSeparator();
    QAction *quitAction = m_menu->addAction(tr("Выход"));

    connect(showAction, &QAction::triggered, this, &NotificationService::showRequested);
    connect(quitAction, &QAction::triggered, this, &NotificationService::quitRequested);

    m_tray->setContextMenu(m_menu);

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger
                    || reason == QSystemTrayIcon::DoubleClick) {
                    emit showRequested();
                }
            });

    connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this]() {
        emit showRequested();
        if (m_lastChatId > 0)
            emit openChatRequested(m_lastChatId);
    });

    if (m_available) {
        m_tray->show();
    } else {
        qWarning() << "Системный трей недоступен — уведомления отключены";
    }
}

NotificationService::~NotificationService()
{
    if (m_tray)
        m_tray->hide();
    delete m_menu;
    m_menu = nullptr;
}

void NotificationService::notify(const QString &title, const QString &body, qint64 chatId)
{
    m_lastChatId = chatId;

    if (!m_available || !m_tray)
        return;

    if (m_window && m_window->isActiveWindow() && !m_window->isMinimized())
        return;

    m_tray->showMessage(title, body, QSystemTrayIcon::Information, 5000);
}

void NotificationService::setUnreadCount(int count)
{
    m_unread = qMax(0, count);
    updateTooltip();

    if (m_window) {
        const QString base = QStringLiteral("MessengerApp");
        m_window->setWindowTitle(m_unread > 0
                                     ? QStringLiteral("(%1) %2").arg(m_unread).arg(base)
                                     : base);
    }
}

void NotificationService::updateTooltip()
{
    if (!m_tray)
        return;

    if (m_unread > 0) {
        m_tray->setToolTip(QStringLiteral("MessengerApp — %1 непрочитанных").arg(m_unread));
    } else {
        m_tray->setToolTip(QStringLiteral("MessengerApp"));
    }
}

} // namespace app
