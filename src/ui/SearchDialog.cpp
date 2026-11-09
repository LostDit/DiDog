#include "ui/SearchDialog.h"

#include "ui/Formatting.h"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace ui {

SearchDialog::SearchDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("SearchDialog"));
    setWindowTitle(tr("Поиск по сообщениям"));
    resize(560, 520);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_query = new QLineEdit(this);
    m_query->setObjectName(QStringLiteral("SearchField"));
    m_query->setPlaceholderText(tr("Что найти?"));
    m_query->setClearButtonEnabled(true);
    layout->addWidget(m_query);

    m_status = new QLabel(tr("Введите минимум два символа."), this);
    m_status->setObjectName(QStringLiteral("EmptySubtitle"));
    layout->addWidget(m_status);

    m_results = new QListWidget(this);
    m_results->setObjectName(QStringLiteral("SearchResults"));
    m_results->setFrameShape(QFrame::NoFrame);
    m_results->setWordWrap(true);
    layout->addWidget(m_results, 1);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(250);

    connect(m_query, &QLineEdit::textChanged, this, &SearchDialog::onQueryChanged);
    connect(m_debounce, &QTimer::timeout, this, [this]() {
        const QString query = m_query->text().trimmed();
        if (query.size() < 2)
            return;
        m_status->setText(tr("Поиск…"));
        emit searchRequested(query);
    });
    connect(m_results, &QListWidget::itemActivated, this, &SearchDialog::onItemActivated);
    connect(m_results, &QListWidget::itemDoubleClicked, this, &SearchDialog::onItemActivated);
}

void SearchDialog::onQueryChanged()
{
    const QString query = m_query->text().trimmed();

    if (query.size() < 2) {
        m_results->clear();
        m_status->setText(tr("Введите минимум два символа."));
        return;
    }

    m_debounce->start();
}

void SearchDialog::setQuery(const QString &query)
{
    m_query->setText(query);
    m_query->setFocus();
}

void SearchDialog::setResults(const QList<data::Message> &messages,
                              const QHash<qint64, QString> &chatNames)
{
    m_results->clear();

    if (messages.isEmpty()) {
        m_status->setText(tr("Ничего не найдено."));
        return;
    }

    m_status->setText(tr("Найдено: %1").arg(messages.size()));

    for (const data::Message &message : messages) {
        const QString chatName = chatNames.value(message.chatId, tr("Чат"));
        const QString snippet = message.body.simplified().left(120);

        auto *item = new QListWidgetItem(
            QStringLiteral("%1  ·  %2\n%3")
                .arg(chatName, format::chatTime(message.createdAt), snippet));
        item->setData(Qt::UserRole, message.chatId);
        item->setData(Qt::UserRole + 1, message.id);
        m_results->addItem(item);
    }
}

void SearchDialog::setStatus(const QString &text)
{
    m_status->setText(text);
}

void SearchDialog::onItemActivated()
{
    QListWidgetItem *item = m_results->currentItem();
    if (!item)
        return;

    emit messageActivated(item->data(Qt::UserRole).toLongLong(),
                          item->data(Qt::UserRole + 1).toLongLong());
    accept();
}

} // namespace ui
