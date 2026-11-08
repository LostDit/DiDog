#include "ui/GroupDialog.h"

#include "app/ChatService.h"
#include "data/Models.h"

#include <algorithm>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {

GroupDialog::GroupDialog(app::ChatService *chats, qint64 chatId, const QSet<qint64> &exclude,
                         QWidget *parent)
    : QDialog(parent)
    , m_chats(chats)
    , m_chatId(chatId)
    , m_exclude(exclude)
{
    setWindowTitle(chatId > 0 ? tr("Добавить участников") : tr("Новая группа"));
    resize(420, 560);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);

    if (m_chatId <= 0) {
        layout->addWidget(new QLabel(tr("Название группы"), this));
        m_title = new QLineEdit(this);
        m_title->setObjectName(QStringLiteral("SearchField"));
        m_title->setPlaceholderText(tr("Например, «Друзья»"));
        m_title->setMaxLength(64);
        layout->addWidget(m_title);
    }

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("SearchField"));
    m_search->setPlaceholderText(tr("Поиск по имени или логину"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("ParticipantList"));
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setFrameShape(QFrame::NoFrame);
    layout->addWidget(m_list, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_submit = buttons->addButton(m_chatId > 0 ? tr("Добавить") : tr("Создать"),
                                  QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_submit, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_chats, &app::ChatService::usersFound, this, &GroupDialog::onUsersFound);
    connect(m_list, &QListWidget::itemChanged, this, &GroupDialog::onItemChanged);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_chats->searchUsers(text.trimmed());
    });

    if (m_title) {
        connect(m_title, &QLineEdit::textChanged, this, [this](const QString &) {
            updateSubmit();
        });
    }

    updateSubmit();
    m_chats->searchUsers(QString());
}

QString GroupDialog::groupTitle() const
{
    return m_title ? m_title->text().trimmed() : QString();
}

QList<qint64> GroupDialog::selectedIds() const
{
    QList<qint64> ids = m_selected.values();
    std::sort(ids.begin(), ids.end());
    return ids;
}

void GroupDialog::onUsersFound(const QString &query, const QList<data::User> &users)
{
    Q_UNUSED(query)
    m_users = users;
    rebuild();
}

void GroupDialog::rebuild()
{
    const QSignalBlocker blocker(m_list);
    m_list->clear();

    for (const data::User &user : m_users) {
        if (m_exclude.contains(user.id))
            continue;

        auto *item = new QListWidgetItem(
            QStringLiteral("%1  @%2").arg(user.displayName, user.username), m_list);
        item->setData(Qt::UserRole, static_cast<qint64>(user.id));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(m_selected.contains(user.id) ? Qt::Checked : Qt::Unchecked);
    }

    updateSubmit();
}

void GroupDialog::onItemChanged(QListWidgetItem *item)
{
    if (!item)
        return;

    const qint64 id = item->data(Qt::UserRole).toLongLong();
    if (item->checkState() == Qt::Checked)
        m_selected.insert(id);
    else
        m_selected.remove(id);

    updateSubmit();
}

void GroupDialog::updateSubmit()
{
    if (!m_submit)
        return;

    const bool titleOk = m_chatId > 0 || groupTitle().size() >= 2;
    m_submit->setEnabled(titleOk && !m_selected.isEmpty());
}

} // namespace ui
