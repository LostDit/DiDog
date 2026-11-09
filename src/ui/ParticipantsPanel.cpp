#include "ui/ParticipantsPanel.h"

#include "app/ChatService.h"
#include "ui/AvatarWidget.h"
#include "ui/GroupDialog.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QStyle>
#include <QVBoxLayout>

namespace ui {
namespace {

QString roleLabel(const QString &role, QObject *context)
{
    if (role == QLatin1String("owner"))
        return context->tr("владелец");
    if (role == QLatin1String("admin"))
        return context->tr("администратор");
    return context->tr("участник");
}

} // namespace

ParticipantsPanel::ParticipantsPanel(app::ChatService *chats, QWidget *parent)
    : QWidget(parent)
    , m_chats(chats)
{
    setObjectName(QStringLiteral("ParticipantsPanel"));
    setFixedWidth(276);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 14, 12, 12);
    layout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    auto *title = new QLabel(tr("Участники"), this);
    title->setObjectName(QStringLiteral("SectionTitle"));
    m_count = new QLabel(QString(), this);
    m_count->setObjectName(QStringLiteral("ChatStatus"));
    headerRow->addWidget(title);
    headerRow->addStretch(1);
    headerRow->addWidget(m_count);
    layout->addLayout(headerRow);

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
    m_addButton = new QPushButton(tr("Добавить"), this);
    m_addButton->setObjectName(QStringLiteral("GhostButton"));
    m_addButton->setCursor(Qt::PointingHandCursor);
    m_addButton->setVisible(false);
    buttons->addWidget(m_addButton);
    buttons->addStretch(1);

    auto *leaveButton = new QPushButton(tr("Покинуть"), this);
    leaveButton->setObjectName(QStringLiteral("LinkButton"));
    leaveButton->setCursor(Qt::PointingHandCursor);
    buttons->addWidget(leaveButton);
    layout->addLayout(buttons);

    connect(m_search, &QLineEdit::textChanged, this, &ParticipantsPanel::onSearchChanged);
    connect(m_addButton, &QPushButton::clicked, this, &ParticipantsPanel::onAddClicked);
    connect(leaveButton, &QPushButton::clicked, this, &ParticipantsPanel::onLeaveClicked);

    if (m_chats) {
        connect(m_chats, &app::ChatService::membersLoaded,
                this, &ParticipantsPanel::onMembersLoaded);
        connect(m_chats, &app::ChatService::membersChanged,
                this, &ParticipantsPanel::onMembersChanged);
    }
}

void ParticipantsPanel::setChat(qint64 chatId, qint64 currentUserId)
{
    if (m_chatId == chatId && m_currentUserId == currentUserId)
        return;

    m_chatId = chatId;
    m_currentUserId = currentUserId;
    m_members.clear();
    m_myRole.clear();
    m_list->clear();
    if (m_count)
        m_count->clear();

    refresh();
}

void ParticipantsPanel::refresh()
{
    if (m_chats && m_chatId > 0)
        m_chats->loadMembers(m_chatId);
}

void ParticipantsPanel::onMembersLoaded(qint64 chatId, const QList<data::ChatMember> &members)
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

void ParticipantsPanel::onMembersChanged(qint64 chatId)
{
    if (chatId == m_chatId)
        refresh();
}

void ParticipantsPanel::onSearchChanged(const QString &text)
{
    Q_UNUSED(text)
    rebuild();
}

bool ParticipantsPanel::canManage() const
{
    return m_myRole == QLatin1String("owner") || m_myRole == QLatin1String("admin");
}

void ParticipantsPanel::rebuild()
{
    m_list->clear();

    if (m_count)
        m_count->setText(QString::number(m_members.size()));

    const QString filter = m_search->text().trimmed().toLower();

    for (const data::ChatMember &member : m_members) {
        if (!filter.isEmpty()
            && !member.displayName.toLower().contains(filter)
            && !member.username.toLower().contains(filter)) {
            continue;
        }

        QString status = roleLabel(member.role, this);
        if (member.online)
            status += QStringLiteral(" · ") + tr("в сети");

        auto *row = new QWidget(m_list);
        row->setObjectName(QStringLiteral("ParticipantRow"));
        row->setAttribute(Qt::WA_StyledBackground, true);
        row->setCursor(Qt::PointingHandCursor);

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(6, 4, 6, 4);
        rowLayout->setSpacing(10);

        auto *avatar = new AvatarWidget(row);
        avatar->setDiameter(34);
        avatar->setIdentity(member.displayName, member.avatarHue);
        avatar->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(avatar);

        auto *column = new QVBoxLayout;
        column->setSpacing(0);
        auto *name = new QLabel(member.displayName, row);
        name->setObjectName(QStringLiteral("ParticipantName"));
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *caption = new QLabel(status, row);
        caption->setObjectName(QStringLiteral("ParticipantStatus"));
        caption->setAttribute(Qt::WA_TransparentForMouseEvents);
        column->addWidget(name);
        column->addWidget(caption);
        rowLayout->addLayout(column, 1);

        auto *item = new QListWidgetItem(m_list);
        item->setSizeHint(row->sizeHint());
        m_list->setItemWidget(item, row);

        row->setProperty("userId", member.userId);
        row->installEventFilter(this);
    }

    if (m_addButton)
        m_addButton->setVisible(canManage());
}

void ParticipantsPanel::onAddClicked()
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

void ParticipantsPanel::onLeaveClicked()
{
    const auto answer = QMessageBox::question(
        this, tr("Покинуть группу"),
        tr("Вы выйдете из группы. История на этом компьютере будет удалена."));
    if (answer != QMessageBox::Yes)
        return;

    m_chats->leaveChat(m_chatId);
    emit closeRequested();
}

bool ParticipantsPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
        if (auto *row = qobject_cast<QWidget *>(watched)) {
            if (row->property("userId").isValid()) {
                row->setProperty("hovered", event->type() == QEvent::Enter);
                if (QStyle *style = row->style()) {
                    style->unpolish(row);
                    style->polish(row);
                }
            }
        }
        return false;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        const qint64 userId = watched->property("userId").toLongLong();
        if (userId > 0) {
            emit profileRequested(userId);
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

} // namespace ui
