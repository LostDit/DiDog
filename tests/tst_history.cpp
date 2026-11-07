#include "app/HistoryStore.h"
#include "core/Ids.h"

#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestHistory : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void persistsChatsAndMessages();
    void survivesReopen();
    void outboxLifecycle();

private:
    data::Message makeMessage(qint64 chatId, qint64 seq, const QString &body);

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<app::HistoryStore> m_store;
};

void TestHistory::init()
{
    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());

    m_store.reset(new app::HistoryStore);
    QString error;
    QVERIFY2(m_store->open(m_dir->filePath(QStringLiteral("client.db")), &error),
             qPrintable(error));
}

void TestHistory::cleanup()
{
    m_store.reset();
    m_dir.reset();
}

data::Message TestHistory::makeMessage(qint64 chatId, qint64 seq, const QString &body)
{
    data::Message message;
    message.id = seq * 10;
    message.chatId = chatId;
    message.senderId = 2;
    message.seq = seq;
    message.clientMsgId = core::newUuid();
    message.kind = data::MessageKind::Text;
    message.body = body;
    message.createdAt = 1700000000000LL + seq * 1000;
    message.senderDisplayName = QStringLiteral("Алиса");
    message.senderHue = 200;
    return message;
}

void TestHistory::persistsChatsAndMessages()
{
    data::ChatSummary summary;
    summary.chat.id = 7;
    summary.chat.kind = QStringLiteral("direct");
    summary.peerId = 3;
    summary.peerDisplayName = QStringLiteral("Боб");
    summary.peerHue = 140;
    summary.lastBody = QStringLiteral("Привет");
    summary.lastAt = 1700000000000LL;
    summary.lastSeq = 1;
    summary.unread = 2;

    QSignalSpy chatsSpy(m_store.data(), &app::HistoryStore::chatsLoaded);
    m_store->saveChats({summary});
    m_store->loadChats();
    QTRY_COMPARE_WITH_TIMEOUT(chatsSpy.count(), 1, 5000);

    const auto loadedChats = chatsSpy.first().at(0).value<QList<data::ChatSummary>>();
    QCOMPARE(loadedChats.size(), 1);
    QCOMPARE(loadedChats.first().chat.id, qint64(7));
    QCOMPARE(loadedChats.first().peerDisplayName, QStringLiteral("Боб"));
    QCOMPARE(loadedChats.first().unread, 2);

    QSignalSpy messagesSpy(m_store.data(), &app::HistoryStore::messagesLoaded);
    m_store->saveMessage(makeMessage(7, 1, QStringLiteral("первое")));
    m_store->saveMessage(makeMessage(7, 2, QStringLiteral("второе")));
    m_store->loadMessages(7, 50);
    QTRY_COMPARE_WITH_TIMEOUT(messagesSpy.count(), 1, 5000);

    const auto loaded = messagesSpy.first().at(1).value<QList<data::Message>>();
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded.first().body, QStringLiteral("первое"));
    QCOMPARE(loaded.last().body, QStringLiteral("второе"));
}

void TestHistory::survivesReopen()
{
    const QString path = m_dir->filePath(QStringLiteral("client.db"));

    {
        app::HistoryStore store;
        QString error;
        QVERIFY2(store.open(path, &error), qPrintable(error));
        store.saveMessage(makeMessage(5, 1, QStringLiteral("до перезапуска")));
        QTest::qWait(200); // даём рабочему потоку записать
    }

    app::HistoryStore reopened;
    QString error;
    QVERIFY2(reopened.open(path, &error), qPrintable(error));

    QSignalSpy spy(&reopened, &app::HistoryStore::messagesLoaded);
    reopened.loadMessages(5, 50);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

    const auto loaded = spy.first().at(1).value<QList<data::Message>>();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().body, QStringLiteral("до перезапуска"));
}

void TestHistory::outboxLifecycle()
{
    data::OutboxEntry entry;
    entry.clientMsgId = QStringLiteral("pending-1");
    entry.chatId = 9;
    entry.kind = data::MessageKind::Text;
    entry.body = QStringLiteral("офлайн");
    entry.createdAt = 1700000000000LL;

    QSignalSpy spy(m_store.data(), &app::HistoryStore::outboxLoaded);

    m_store->addOutbox(entry);
    m_store->loadOutbox();
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    QCOMPARE(spy.first().at(0).value<QList<data::OutboxEntry>>().size(), 1);

    spy.clear();
    m_store->removeOutbox(entry.clientMsgId);
    m_store->loadOutbox();
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    QCOMPARE(spy.first().at(0).value<QList<data::OutboxEntry>>().size(), 0);
}

QTEST_GUILESS_MAIN(TestHistory)
#include "tst_history.moc"
