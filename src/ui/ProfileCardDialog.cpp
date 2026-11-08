#include "ui/ProfileCardDialog.h"

#include "app/ChatService.h"
#include "ui/AvatarWidget.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {

ProfileCardDialog::ProfileCardDialog(app::ChatService *chats, qint64 chatId, qint64 userId,
                                     qint64 currentUserId, QWidget *parent)
    : QDialog(parent)
    , m_chats(chats)
    , m_chatId(chatId)
    , m_userId(userId)
    , m_currentUserId(currentUserId)
{
    setWindowTitle(tr("Профиль"));
    setModal(true);
    setFixedWidth(360);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 16);
    layout->setSpacing(0);

    m_content = new QWidget(this);
    layout->addWidget(m_content);

    if (m_chats && m_chatId > 0) {
        connect(m_chats, &app::ChatService::membersLoaded,
                this, &ProfileCardDialog::onMembersLoaded);
        m_chats->loadMembers(m_chatId);
    }

    buildUi(QString(), QString(), 210, false, false);
}

void ProfileCardDialog::onMembersLoaded(qint64 chatId, const QList<data::ChatMember> &members)
{
    if (chatId != m_chatId || m_loaded)
        return;

    for (const data::ChatMember &member : members) {
        if (member.userId != m_userId)
            continue;

        m_displayName = member.displayName;
        m_username = member.username;
        m_hue = member.avatarHue;
        m_online = member.online;
        m_loaded = true;
        buildUi(m_displayName, m_username, m_hue, m_online, true);
        return;
    }
}

void ProfileCardDialog::buildUi(const QString &displayName, const QString &username, int hue,
                                bool online, bool known)
{
    auto *layout = qobject_cast<QVBoxLayout *>(m_content->layout());
    if (!layout) {
        layout = new QVBoxLayout(m_content);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    } else {
        while (QLayoutItem *item = layout->takeAt(0)) {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
    }

    auto *banner = new QFrame(m_content);
    banner->setFixedHeight(96);
    QColor bannerColor = QColor::fromHsv(qBound(0, hue, 359), 150, 205);
    banner->setStyleSheet(QStringLiteral("background: %1;").arg(bannerColor.name()));
    layout->addWidget(banner);

    auto *avatar = new AvatarWidget(m_content);
    avatar->setDiameter(84);
    avatar->setIdentity(known ? displayName : QStringLiteral("?"), hue);
    auto *avatarRow = new QHBoxLayout;
    avatarRow->setContentsMargins(0, -42, 0, 0);
    avatarRow->addStretch(1);
    avatarRow->addWidget(avatar);
    avatarRow->addStretch(1);
    layout->addLayout(avatarRow);

    auto *name = new QLabel(known ? displayName : tr("Загрузка…"), m_content);
    name->setObjectName(QStringLiteral("ChatTitle"));
    name->setAlignment(Qt::AlignCenter);
    name->setContentsMargins(16, 10, 16, 0);
    layout->addWidget(name);

    auto *login = new QLabel(known && !username.isEmpty() ? QStringLiteral("@%1").arg(username)
                                                          : QString(),
                             m_content);
    login->setObjectName(QStringLiteral("ChatStatus"));
    login->setAlignment(Qt::AlignCenter);
    layout->addWidget(login);

    if (known) {
        auto *status = new QLabel(online ? tr("в сети") : tr("не в сети"), m_content);
        status->setObjectName(QStringLiteral("EmptySubtitle"));
        status->setAlignment(Qt::AlignCenter);
        layout->addWidget(status);
    }

    layout->addSpacing(14);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(20, 0, 20, 0);
    buttons->addStretch(1);

    const bool canWrite = known && m_userId > 0 && m_userId != m_currentUserId
                          && !username.isEmpty();
    if (canWrite) {
        auto *write = new QPushButton(tr("Написать"), m_content);
        write->setObjectName(QStringLiteral("PrimaryButton"));
        write->setCursor(Qt::PointingHandCursor);
        connect(write, &QPushButton::clicked, this, [this]() {
            if (m_chats && !m_username.isEmpty()) {
                m_chats->createDirect(m_username);
                accept();
            }
        });
        buttons->addWidget(write);
        buttons->addSpacing(8);
    }

    auto *close = new QPushButton(tr("Закрыть"), m_content);
    close->setObjectName(QStringLiteral("GhostButton"));
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close);
    buttons->addStretch(1);

    layout->addLayout(buttons);
}

} // namespace ui
