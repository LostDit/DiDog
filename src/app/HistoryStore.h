#pragma once

#include "data/Database.h"
#include "data/Models.h"

#include <QList>
#include <QObject>
#include <QString>

class QThread;

namespace app {

// Рабочий объект, который живёт в отдельном потоке и владеет соединением SQLite.
// Ни один метод не вызывается из GUI-потока напрямую.
class HistoryWorker : public QObject
{
    Q_OBJECT

public:
    explicit HistoryWorker(QObject *parent = nullptr);

    bool open(const QString &databasePath, QString *error);
    void loadChats();
    void saveChats(const QList<data::ChatSummary> &chats);
    void loadMessages(qint64 chatId, int limit);
    void saveMessage(const data::Message &message);
    void addOutbox(const data::OutboxEntry &entry);
    void removeOutbox(const QString &clientMsgId);
    void loadOutbox();
    void searchMessages(const QString &query, int limit);
    void removeChat(qint64 chatId);

signals:
    void chatsLoaded(const QList<data::ChatSummary> &chats);
    void messagesLoaded(qint64 chatId, const QList<data::Message> &messages);
    void outboxLoaded(const QList<data::OutboxEntry> &entries);
    void searchResults(const QString &query, const QList<data::Message> &messages);
    void failed(const QString &message);

private:
    void report(const QString &error);

    data::Database m_database{QStringLiteral("client_history")};
    bool m_open = false;
};

// Фасад для GUI-потока: очередь задач в рабочий поток и сигналы с результатами.
class HistoryStore : public QObject
{
    Q_OBJECT

public:
    explicit HistoryStore(QObject *parent = nullptr);
    ~HistoryStore() override;

    // Открывает базу в рабочем потоке; вызывается один раз при старте приложения.
    bool open(const QString &databasePath, QString *error);
    bool isOpen() const { return m_open; }

    void loadChats();
    void saveChats(const QList<data::ChatSummary> &chats);
    void loadMessages(qint64 chatId, int limit = 100);
    void saveMessage(const data::Message &message);
    void addOutbox(const data::OutboxEntry &entry);
    void removeOutbox(const QString &clientMsgId);
    void loadOutbox();
    void searchMessages(const QString &query, int limit = 50);
    void removeChat(qint64 chatId);

signals:
    void chatsLoaded(const QList<data::ChatSummary> &chats);
    void messagesLoaded(qint64 chatId, const QList<data::Message> &messages);
    void outboxLoaded(const QList<data::OutboxEntry> &entries);
    void searchResults(const QString &query, const QList<data::Message> &messages);
    void failed(const QString &message);

private:
    void submit(std::function<void(HistoryWorker *)> task);

    QThread *m_thread = nullptr;
    HistoryWorker *m_worker = nullptr;
    bool m_open = false;
};

} // namespace app
