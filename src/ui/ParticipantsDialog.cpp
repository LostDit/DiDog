#include "ui/ParticipantsDialog.h"

#include "app/ChatService.h"
#include "ui/AvatarWidget.h"
#include "ui/GroupDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

namespace ui {

ParticipantsDialog::ParticipantsDialog(app::ChatService *chats, qint64 chatId,
                                       qint64 currentUserId, QWidget *parent)
    : QDialog(parent)
    , m_chats(chats)
    , m_chatId(chatId)
    , m_currentUserId(currentUserId)
{
    setWindowTitle(tr("Участники"));
    resize(400, 560);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("SearchField"));
    m_search->setPlaceholderText(tr("Поиск участников"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("ParticipantList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_list, 1);

    auto *buttons = new QHBoxLayout;
    auto *addButton = new QPushButton(tr("Добавить"), this);
    addButton->setObjectName(QStringLiteral("PrimaryButton"));
    addButton->setVisible(false);
    buttons->addWidget(addButton);
    buttons->addStretch(1);

    auto *leaveButton = new QPushButton(tr("Покинуть группу"), this);
    leaveButton->setObjectName(QStringLiteral("LinkButton"));
    buttons->addWidget(leaveButton);

    auto *closeButton = new QPushButton(tr("Закрыть"), this);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    m_addButton = addButton;

    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(addButton, &QPushButton::clicked, this, &ParticipantsDialog::onAddClicked);
    connect(leaveButton, &QPushButton::clicked, this, &ParticipantsDialog::onLeaveClicked);
    connect(m_search, &QLineEdit::textChanged, this, &ParticipantsDialog::onSearchChanged);
    connect(m_chats, &app::ChatService::membersLoaded,
            this, &ParticipantsDialog::onMembersLoaded);
    connect(m_chats, &app::ChatService::membersChanged,
            this, &ParticipantsDialog::onMembersChanged);

    m_chats->loadMembers(m_chatId);
}

void ParticipantsDialog::onMembersLoaded(qint64 chatId,
                                         const QList<data::ChatMember> &members)
{
    if (chatId != m_chatId)
        return;

    m_members = members;
    m_myRole.clear();
    for (const data::ChatMember &member : m_members) {
        if (member.userId == m_currentUserId) {
            m_myRole = member.role;
            break;
        }
    }

    rebuild();
}

void ParticipantsDialog::onMembersChanged(qint64 chatId)
{
    if (chatId == m_chatId)
        m_chats->loadMembers(m_chatId);
}

void ParticipantsDialog::onSearchChanged(const QString &text)
{
    Q_UNUSED(text)
    rebuild();
}

bool ParticipantsDialog::canManage() const
{
    return m_myRole == QLatin1String("owner") || m_myRole == QLatin1String("admin");
}

void ParticipantsDialog::rebuild()
{
    m_list->clear();

    const QString filter = m_search->text().trimmed().toLower();

    for (const data::ChatMember &member : m_members) {
        if (!filter.isEmpty()
            && !member.displayName.toLower().contains(filter)
            && !member.username.toLower().contains(filter)) {
            continue;
        }

        QString roleText;
        if (member.role == QLatin1String("owner"))
            roleText = tr("владелец");
        else if (member.role == QLatin1String("admin"))
            roleText = tr("администратор");
        else
            roleText = tr("участник");

        QString status = roleText;
        if (member.online)
            status += QStringLiteral(" · ") + tr("в сети");

        auto *row = new QWidget(m_list);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(6, 4, 6, 4);
        rowLayout->setSpacing(10);

        auto *avatar = new AvatarWidget(row);
        avatar->setDiameter(36);
        avatar->setIdentity(member.displayName, member.avatarHue);
        rowLayout->addWidget(avatar);

        auto *column = new QVBoxLayout;
        column->setSpacing(0);
        auto *name = new QLabel(member.displayName, row);
        name->setObjectName(QStringLiteral("ParticipantName"));
        auto *caption = new QLabel(status, row);
        caption->setObjectName(QStringLiteral("ParticipantStatus"));
        column->addWidget(name);
        column->addWidget(caption);
        rowLayout->addLayout(column, 1);

        auto *item = new QListWidgetItem(m_list);
        item->setSizeHint(row->sizeHint());
        m_list->setItemWidget(item, row);
    }

    if (m_addButton)
        m_addButton->setVisible(canManage());
}

void ParticipantsDialog::onAddClicked()
{
    QSet<qint64> existing;
    for (const data::ChatMember &member : m_members)
        existing.insert(member.userId);

    GroupDialog dialog(m_chats, m_chatId, existing, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QList<qint64> ids = dialog.selectedIds();
    if (!ids.isEmpty())
        m_chats->addMembers(m_chatId, ids);
}

void ParticipantsDialog::onLeaveClicked()
{
    const auto answer = QMessageBox::question(
        this, tr("Покинуть группу"),
        tr("Вы выйдете из группы. История на этом компьютере будет удалена."));
    if (answer != QMessageBox::Yes)
        return;

    m_chats->leaveChat(m_chatId);
    accept();
}

} // namespace ui
