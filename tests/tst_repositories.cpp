#include "core/Ids.h"
#include "data/AttachmentRepository.h"
#include "data/ChatRepository.h"
#include "data/Database.h"
#include "data/MessageRepository.h"
#include "data/OutboxRepository.h"
#include "data/ReceiptRepository.h"
#include "data/UserRepository.h"
#include "security/PasswordHasher.h"

#include <QDateTime>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QtTest>

class TestRepositories : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void createsAndFindsUser();
    void usernameLookupIsCaseInsensitive();
    void directChatIsReused();
    void chatSummaryShowsPeerAndUnread();
    void messagesGetSequentialNumbers();
    void appendIsIdempotentByClientId();
    void historyIsPaginatedBackwards();
    void receiptsAggregateToRead();
    void attachmentRoundTrip();
    void outboxLifecycle();

private:
    data::User createUser(const QString &username, const QString &displayName);
    data::Message appendText(qint64 chatId, qint64 senderId, const QString &body);

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<data::Database> m_database;
    qint64 m_aliceId = 0;
    qint64 m_bobId = 0;
    qint64 m_chatId = 0;
};

void TestRepositories::init()
{
    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());

    m_database.reset(new data::Database(QStringLiteral("tst_repo_db")));
    QString error;
    QVERIFY2(m_database->open(m_dir->filePath(QStringLiteral("test.db")), &error),
             qPrintable(error));

    m_aliceId = createUser(QStringLiteral("alice"), QStringLiteral("Алиса")).id;
    m_bobId = createUser(QStringLiteral("bob"), QStringLiteral("Боб")).id;

    data::ChatRepository chats(m_database->handle());
    data::Chat chat;
    QVERIFY2(chats.createDirect(m_aliceId, m_bobId, &chat, &error), qPrintable(error));
    m_chatId = chat.id;
}

void TestRepositories::cleanup()
{
    m_database.reset();
    m_dir.reset();
}

data::User TestRepositories::createUser(const QString &username, const QString &displayName)
{
    data::UserRepository users(m_database->handle());
    data::User created;
    QString error;
    const security::PasswordHash hash =
        security::PasswordHasher::create(QStringLiteral("password123"));
    const bool ok = users.insert(username, displayName, hash, 120, &created, &error);
    Q_ASSERT_X(ok, "createUser", qPrintable(error));
    return created;
}

data::Message TestRepositories::appendText(qint64 chatId, qint64 senderId, const QString &body)
{
    data::MessageRepository messages(m_database->handle());
    data::Message message;
    message.chatId = chatId;
    message.senderId = senderId;
    message.clientMsgId = core::newUuid();
    message.kind = data::MessageKind::Text;
    message.body = body;
    message.createdAt = QDateTime::currentMSecsSinceEpoch();

    data::Message stored;
    QString error;
    const bool ok = messages.append(message, &stored, &error);
    Q_ASSERT_X(ok, "appendText", qPrintable(error));
    return stored;
}

void TestRepositories::createsAndFindsUser()
{
    data::UserRepository users(m_database->handle());
    QString error;
    data::User found;

    QVERIFY2(users.findByUsername(QStringLiteral("alice"), &found, &error), qPrintable(error));
    QCOMPARE(found.id, m_aliceId);
    QCOMPARE(found.displayName, QStringLiteral("Алиса"));

    int count = 0;
    QVERIFY(users.count(&count, &error));
    QCOMPARE(count, 2);

    // Пароль сохранён и проверяется.
    data::User authUser;
    security::PasswordHash hash;
    QVERIFY(users.findAuth(QStringLiteral("alice"), &authUser, &hash, &error));
    QVERIFY(security::PasswordHasher::verify(QStringLiteral("password123"), hash));
    QVERIFY(!security::PasswordHasher::verify(QStringLiteral("wrong"), hash));
}

void TestRepositories::usernameLookupIsCaseInsensitive()
{
    data::UserRepository users(m_database->handle());
    bool exists = false;
    QString error;

    QVERIFY(users.usernameExists(QStringLiteral("ALICE"), &exists, &error));
    QVERIFY(exists);

    data::User found;
    QVERIFY(users.findByUsername(QStringLiteral("Alice"), &found, &error));
    QCOMPARE(found.id, m_aliceId);
}

void TestRepositories::directChatIsReused()
{
    data::ChatRepository chats(m_database->handle());
    data::Chat again;
    QString error;

    QVERIFY2(chats.createDirect(m_bobId, m_aliceId, &again, &error), qPrintable(error));
    QCOMPARE(again.id, m_chatId);

    bool member = false;
    QVERIFY(chats.isMember(m_chatId, m_bobId, &member, &error));
    QVERIFY(member);

    QList<qint64> ids;
    QVERIFY(chats.memberIds(m_chatId, &ids, &error));
    QCOMPARE(ids.size(), 2);
}

void TestRepositories::chatSummaryShowsPeerAndUnread()
{
    data::ChatRepository chats(m_database->handle());
    QString error;

    appendText(m_chatId, m_bobId, QStringLiteral("Привет!"));

    data::ChatSummary forAlice;
    QVERIFY2(chats.summaryFor(m_chatId, m_aliceId, &forAlice, &error), qPrintable(error));
    QCOMPARE(forAlice.peerId, m_bobId);
    QCOMPARE(forAlice.peerDisplayName, QStringLiteral("Боб"));
    QCOMPARE(forAlice.unread, 1);
    QCOMPARE(forAlice.lastBody, QStringLiteral("Привет!"));
    QCOMPARE(forAlice.lastSeq, qint64(1));

    data::ChatSummary forBob;
    QVERIFY(chats.summaryFor(m_chatId, m_bobId, &forBob, &error));
    QCOMPARE(forBob.peerId, m_aliceId);
    QCOMPARE(forBob.unread, 0);

    QVERIFY(chats.setLastRead(m_chatId, m_aliceId, 1, &error));

    data::ChatSummary afterRead;
    QVERIFY(chats.summaryFor(m_chatId, m_aliceId, &afterRead, &error));
    QCOMPARE(afterRead.unread, 0);

    QList<data::ChatSummary> list;
    QVERIFY(chats.summaries(m_aliceId, &list, &error));
    QCOMPARE(list.size(), 1);
}

void TestRepositories::messagesGetSequentialNumbers()
{
    const data::Message first = appendText(m_chatId, m_aliceId, QStringLiteral("раз"));
    const data::Message second = appendText(m_chatId, m_aliceId, QStringLiteral("два"));
    const data::Message third = appendText(m_chatId, m_bobId, QStringLiteral("три"));

    QCOMPARE(first.seq, qint64(1));
    QCOMPARE(second.seq, qint64(2));
    QCOMPARE(third.seq, qint64(3));

    QVERIFY(first.id > 0);

    data::MessageRepository messages(m_database->handle());
    qint64 maxSeq = 0;
    QString error;
    QVERIFY(messages.maxSeq(m_chatId, &maxSeq, &error));
    QCOMPARE(maxSeq, qint64(3));
}

void TestRepositories::appendIsIdempotentByClientId()
{
    data::MessageRepository messages(m_database->handle());
    data::Message original;
    original.chatId = m_chatId;
    original.senderId = m_aliceId;
    original.clientMsgId = core::newUuid();
    original.kind = data::MessageKind::Text;
    original.body = QStringLiteral("одно и то же");
    original.createdAt = QDateTime::currentMSecsSinceEpoch();

    data::Message first;
    data::Message second;
    QString error;
    QVERIFY2(messages.append(original, &first, &error), qPrintable(error));
    QVERIFY2(messages.append(original, &second, &error), qPrintable(error));

    QCOMPARE(first.id, second.id);
    QCOMPARE(first.seq, second.seq);

    int count = 0;
    QVERIFY(messages.countInChat(m_chatId, &count, &error));
    QCOMPARE(count, 1);
}

void TestRepositories::historyIsPaginatedBackwards()
{
    for (int i = 1; i <= 60; ++i)
        appendText(m_chatId, m_aliceId, QStringLiteral("сообщение %1").arg(i));

    data::MessageRepository messages(m_database->handle());
    QString error;

    QList<data::Message> latest;
    bool hasMore = false;
    QVERIFY2(messages.page(m_chatId, 0, 20, &latest, &hasMore, &error), qPrintable(error));
    QCOMPARE(latest.size(), 20);
    QCOMPARE(latest.first().seq, qint64(41));
    QCOMPARE(latest.last().seq, qint64(60));
    QVERIFY(hasMore);

    QList<data::Message> older;
    QVERIFY(messages.page(m_chatId, 41, 20, &older, &hasMore, &error));
    QCOMPARE(older.size(), 20);
    QCOMPARE(older.first().seq, qint64(21));
    QCOMPARE(older.last().seq, qint64(40));
    QVERIFY(hasMore);

    QList<data::Message> oldest;
    QVERIFY(messages.page(m_chatId, 1, 20, &oldest, &hasMore, &error));
    QVERIFY(oldest.isEmpty());
    QVERIFY(!hasMore);
}

void TestRepositories::receiptsAggregateToRead()
{
    const data::Message message = appendText(m_chatId, m_aliceId, QStringLiteral("статус"));

    data::ReceiptRepository receipts(m_database->handle());
    QString error;
    data::DeliveryState state = data::DeliveryState::Sent;

    QVERIFY(receipts.stateForMessage(message.id, m_aliceId, &state, &error));
    QCOMPARE(state, data::DeliveryState::Sent);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(receipts.add(message.id, m_bobId, QStringLiteral("delivered"), now, &error));
    QVERIFY(receipts.stateForMessage(message.id, m_aliceId, &state, &error));
    QCOMPARE(state, data::DeliveryState::Delivered);

    QVERIFY(receipts.add(message.id, m_bobId, QStringLiteral("read"), now + 1, &error));
    QVERIFY(receipts.stateForMessage(message.id, m_aliceId, &state, &error));
    QCOMPARE(state, data::DeliveryState::Read);
}

void TestRepositories::attachmentRoundTrip()
{
    const data::Message message = appendText(m_chatId, m_aliceId, QStringLiteral("фото"));

    data::AttachmentRepository attachments(m_database->handle());
    data::Attachment attachment;
    attachment.messageId = message.id;
    attachment.fileName = QStringLiteral("photo.jpg");
    attachment.mime = QStringLiteral("image/jpeg");
    attachment.byteSize = 12345;
    attachment.sha256 = QString(64, QLatin1Char('a'));
    attachment.relPath = QStringLiteral("ab/aaaaaaaa.jpg");
    attachment.width = 800;
    attachment.height = 600;

    QString error;
    data::Attachment stored;
    QVERIFY2(attachments.insert(attachment, &stored, &error), qPrintable(error));
    QVERIFY(stored.id > 0);

    data::Attachment found;
    bool exists = false;
    QVERIFY(attachments.byMessageId(message.id, &found, &exists, &error));
    QVERIFY(exists);
    QCOMPARE(found.fileName, QStringLiteral("photo.jpg"));
    QCOMPARE(found.byteSize, qint64(12345));
    QCOMPARE(found.width, 800);
    QCOMPARE(found.durationMs, 0); // NULL превращается в 0
}

void TestRepositories::outboxLifecycle()
{
    data::OutboxRepository outbox(m_database->handle());
    QString error;

    data::OutboxEntry entry;
    entry.clientMsgId = core::newUuid();
    entry.chatId = m_chatId;
    entry.kind = data::MessageKind::Text;
    entry.body = QStringLiteral("офлайн");
    entry.createdAt = QDateTime::currentMSecsSinceEpoch();

    QVERIFY2(outbox.add(entry, &error), qPrintable(error));

    int count = 0;
    QVERIFY(outbox.count(&count, &error));
    QCOMPARE(count, 1);

    QList<data::OutboxEntry> pending;
    QVERIFY(outbox.pending(m_chatId, &pending, &error));
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending.first().body, QStringLiteral("офлайн"));

    QVERIFY(outbox.remove(entry.clientMsgId, &error));
    QVERIFY(outbox.count(&count, &error));
    QCOMPARE(count, 0);
}

QTEST_GUILESS_MAIN(TestRepositories)
#include "tst_repositories.moc"
