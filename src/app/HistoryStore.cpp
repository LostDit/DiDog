#include "app/HistoryStore.h"

#include "data/ChatCacheRepository.h"
#include "data/MessageRepository.h"
#include "data/OutboxRepository.h"

#include <QDebug>
#include <QMetaObject>
#include <QThread>

#include <functional>
#include <utility>

namespace app {

HistoryWorker::HistoryWorker(QObject *parent)
    : QObject(parent)
{
}

bool HistoryWorker::open(const QString &databasePath, QString *error)
{
    if (!m_open) {
        if (!m_database.open(databasePath, error))
            return false;
        m_open = true;
    }
    return true;
}

void HistoryWorker::loadChats()
{
    if (!m_open)
        return;

    data::ChatCacheRepository cache(m_database.handle());
    QList<data::ChatSummary> chats;
    QString error;

    if (!cache.all(&chats, &error)) {
        report(error);
        return;
    }

    emit chatsLoaded(chats);
}

void HistoryWorker::saveChats(const QList<data::ChatSummary> &chats)
{
    if (!m_open)
        return;

    data::ChatCacheRepository cache(m_database.handle());
    QString error;
    if (!cache.replaceAll(chats, &error))
        report(error);
}

void HistoryWorker::loadMessages(qint64 chatId, int limit)
{
    if (!m_open)
        return;

    data::MessageRepository messages(m_database.handle());
    QList<data::Message> page;
    bool hasMore = false;
    QString error;

    if (!messages.page(chatId, 0, limit, &page, &hasMore, &error)) {
        report(error);
        return;
    }

    emit messagesLoaded(chatId, page);
}

void HistoryWorker::saveMessage(const data::Message &message)
{
    if (!m_open || message.id <= 0)
        return;

    data::MessageRepository messages(m_database.handle());
    bool inserted = false;
    QString error;

    if (!messages.store(message, &inserted, &error))
        report(error);
}

void HistoryWorker::addOutbox(const data::OutboxEntry &entry)
{
    if (!m_open)
        return;

    data::OutboxRepository outbox(m_database.handle());
    QString error;
    if (!outbox.add(entry, &error))
        report(error);
}

void HistoryWorker::removeOutbox(const QString &clientMsgId)
{
    if (!m_open)
        return;

    data::OutboxRepository outbox(m_database.handle());
    QString error;
    if (!outbox.remove(clientMsgId, &error))
        report(error);
}

void HistoryWorker::loadOutbox()
{
    if (!m_open)
        return;

    data::OutboxRepository outbox(m_database.handle());
    QList<data::OutboxEntry> entries;
    QString error;

    if (!outbox.allPending(&entries, &error)) {
        report(error);
        return;
    }

    emit outboxLoaded(entries);
}

void HistoryWorker::searchMessages(const QString &query, int limit)
{
    if (!m_open)
        return;

    data::MessageRepository messages(m_database.handle());
    QList<data::Message> found;
    QString error;

    if (!messages.search(query, limit, &found, &error)) {
        report(error);
        return;
    }

    emit searchResults(query, found);
}

void HistoryWorker::removeChat(qint64 chatId)
{
    if (!m_open)
        return;

    data::MessageRepository messages(m_database.handle());
    QString error;
    if (!messages.removeChat(chatId, &error))
        report(error);
}

void HistoryWorker::removeMessage(qint64 messageId)
{
    if (!m_open)
        return;

    data::MessageRepository messages(m_database.handle());
    QString error;
    if (!messages.removeMessage(messageId, &error))
        report(error);
}

void HistoryWorker::report(const QString &error)
{
    qWarning().noquote() << QStringLiteral("История: %1").arg(error);
    emit failed(error);
}

HistoryStore::HistoryStore(QObject *parent)
    : QObject(parent)
{
}

HistoryStore::~HistoryStore()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
}

bool HistoryStore::open(const QString &databasePath, QString *error)
{
    if (m_open)
        return true;

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("history"));

    m_worker = new HistoryWorker;
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &HistoryWorker::chatsLoaded, this, &HistoryStore::chatsLoaded,
            Qt::QueuedConnection);
    connect(m_worker, &HistoryWorker::messagesLoaded, this, &HistoryStore::messagesLoaded,
            Qt::QueuedConnection);
    connect(m_worker, &HistoryWorker::outboxLoaded, this, &HistoryStore::outboxLoaded,
            Qt::QueuedConnection);
    connect(m_worker, &HistoryWorker::failed, this, &HistoryStore::failed,
            Qt::QueuedConnection);
    connect(m_worker, &HistoryWorker::searchResults, this, &HistoryStore::searchResults,
            Qt::QueuedConnection);

    m_thread->start();

    bool ok = false;
    QString openError;
    HistoryWorker *worker = m_worker;

    // Блокирующий вызов безопасен: выполняется один раз при старте, поток уже запущен.
    QMetaObject::invokeMethod(
        worker,
        [worker, databasePath, &ok, &openError]() { ok = worker->open(databasePath, &openError); },
        Qt::BlockingQueuedConnection);

    m_open = ok;

    if (!ok && error)
        *error = openError;

    if (!ok) {
        m_thread->quit();
        m_thread->wait();
    }

    return ok;
}

void HistoryStore::submit(std::function<void(HistoryWorker *)> task)
{
    if (!m_open || !m_worker)
        return;

    HistoryWorker *worker = m_worker;
    QMetaObject::invokeMethod(
        worker, [worker, task = std::move(task)]() { task(worker); }, Qt::QueuedConnection);
}

void HistoryStore::loadChats()
{
    submit([](HistoryWorker *worker) { worker->loadChats(); });
}

void HistoryStore::saveChats(const QList<data::ChatSummary> &chats)
{
    submit([chats](HistoryWorker *worker) { worker->saveChats(chats); });
}

void HistoryStore::loadMessages(qint64 chatId, int limit)
{
    submit([chatId, limit](HistoryWorker *worker) { worker->loadMessages(chatId, limit); });
}

void HistoryStore::saveMessage(const data::Message &message)
{
    submit([message](HistoryWorker *worker) { worker->saveMessage(message); });
}

void HistoryStore::addOutbox(const data::OutboxEntry &entry)
{
    submit([entry](HistoryWorker *worker) { worker->addOutbox(entry); });
}

void HistoryStore::removeOutbox(const QString &clientMsgId)
{
    submit([clientMsgId](HistoryWorker *worker) { worker->removeOutbox(clientMsgId); });
}

void HistoryStore::loadOutbox()
{
    submit([](HistoryWorker *worker) { worker->loadOutbox(); });
}

void HistoryStore::searchMessages(const QString &query, int limit)
{
    submit([query, limit](HistoryWorker *worker) {
        worker->searchMessages(query, limit);
    });
}

void HistoryStore::removeChat(qint64 chatId)
{
    submit([chatId](HistoryWorker *worker) { worker->removeChat(chatId); });
}

void HistoryStore::removeMessage(qint64 messageId)
{
    submit([messageId](HistoryWorker *worker) { worker->removeMessage(messageId); });
}

} // namespace app
