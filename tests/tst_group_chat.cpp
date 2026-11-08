#include "core/Protocol.h"
#include "net/ClientConnection.h"
#include "server/ServerApp.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

bool hasEvent(const QSignalSpy &spy, const QString &name)
{
    for (const QList<QVariant> &arguments : spy) {
        if (arguments.value(0).toString() == name)
            return true;
    }
    return false;
}

} // namespace

// Групповые чаты: создание с участниками, список участников и роли,
// добавление только владельцем/админом, выход из группы.
class TestGroupChat : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void createMembersAndLeave();

private:
    QJsonObject request(net::ClientConnection &connection, const QString &type,
                        const QJsonObject &data, bool *ok = nullptr);
    void registerUser(net::ClientConnection &connection, const QString &username);

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<server::ServerApp> m_server;
};

void TestGroupChat::init()
{
    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());

    m_server.reset(new server::ServerApp);

    QString error;
    QVERIFY2(m_server->initialize(m_dir->filePath(QStringLiteral("server.db")),
                                  m_dir->filePath(QStringLiteral("attachments")), &error),
             qPrintable(error));
    QVERIFY2(m_server->start(0), qPrintable(m_server->errorString()));
}

void TestGroupChat::cleanup()
{
    m_server.reset();
    m_dir.reset();
}

QJsonObject TestGroupChat::request(net::ClientConnection &connection, const QString &type,
                                   const QJsonObject &data, bool *ok)
{
    bool done = false;
    QJsonObject reply;

    connection.request(type, data, [&done, &reply](const QJsonObject &value) {
        reply = value;
        done = true;
    });

    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < 10000)
        QTest::qWait(10);

    if (ok)
        *ok = done && reply.value(QStringLiteral("ok")).toBool(false);

    return reply;
}

void TestGroupChat::registerUser(net::ClientConnection &connection, const QString &username)
{
    bool ok = false;
    request(connection, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), username},
                        {QStringLiteral("display_name"), username},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);
}

void TestGroupChat::createMembersAndLeave()
{
    net::ClientConnection first;
    net::ClientConnection second;
    net::ClientConnection third;
    first.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    second.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    third.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    QSignalSpy firstConnected(&first, &net::ClientConnection::socketConnected);
    QSignalSpy secondConnected(&second, &net::ClientConnection::socketConnected);
    QSignalSpy thirdConnected(&third, &net::ClientConnection::socketConnected);
    QSignalSpy secondEvents(&second, &net::ClientConnection::eventReceived);
    QSignalSpy thirdEvents(&third, &net::ClientConnection::eventReceived);

    first.connectToServer();
    second.connectToServer();
    third.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(firstConnected.count(), 1, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(secondConnected.count(), 1, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(thirdConnected.count(), 1, 15000);

    registerUser(first, QStringLiteral("owner_user"));
    registerUser(second, QStringLiteral("member_user"));
    registerUser(third, QStringLiteral("third_user"));

    // Находим id второго и третьего через поиск.
    bool ok = false;
    const QJsonObject searchReply = request(
        first, QStringLiteral("user.search"),
        QJsonObject{{QStringLiteral("query"), QStringLiteral("member_user")}}, &ok);
    QVERIFY(ok);
    const QJsonArray found = searchReply.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("users")).toArray();
    QVERIFY(!found.isEmpty());
    const double secondId = found.first().toObject().value(QStringLiteral("id")).toDouble();

    const QJsonObject thirdSearch = request(
        first, QStringLiteral("user.search"),
        QJsonObject{{QStringLiteral("query"), QStringLiteral("third_user")}}, &ok);
    QVERIFY(ok);
    const double thirdId = thirdSearch.value(QStringLiteral("data")).toObject()
                               .value(QStringLiteral("users")).toArray()
                               .first().toObject().value(QStringLiteral("id")).toDouble();

    // Создание группы: второй участник получает ev.chat.
    const QJsonObject createReply = request(
        first, QStringLiteral("chat.create"),
        QJsonObject{{QStringLiteral("title"), QStringLiteral("Тестовая группа")},
                    {QStringLiteral("member_ids"), QJsonArray{secondId}}},
        &ok);
    QVERIFY(ok);
    const QJsonObject chat = createReply.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("chat")).toObject();
    const double chatId = chat.value(QStringLiteral("id")).toDouble();
    QCOMPARE(chat.value(QStringLiteral("kind")).toString(), QStringLiteral("group"));
    QCOMPARE(chat.value(QStringLiteral("title")).toString(), QStringLiteral("Тестовая группа"));
    QVERIFY(chatId > 0);

    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(secondEvents, QStringLiteral("ev.chat")), 10000);

    // Список участников: владелец и участник.
    const QJsonObject membersReply = request(
        first, QStringLiteral("chat.members"),
        QJsonObject{{QStringLiteral("chat_id"), chatId}}, &ok);
    QVERIFY(ok);
    const QJsonArray members = membersReply.value(QStringLiteral("data")).toObject()
                                   .value(QStringLiteral("members")).toArray();
    QCOMPARE(members.size(), 2);

    QString ownerRole;
    QString memberRole;
    for (const QJsonValue &value : members) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("username")).toString() == QLatin1String("owner_user"))
            ownerRole = entry.value(QStringLiteral("role")).toString();
        if (entry.value(QStringLiteral("username")).toString() == QLatin1String("member_user"))
            memberRole = entry.value(QStringLiteral("role")).toString();
    }
    QCOMPARE(ownerRole, QStringLiteral("owner"));
    QCOMPARE(memberRole, QStringLiteral("member"));

    // Участник не может добавлять людей.
    const QJsonObject forbidden = request(
        second, QStringLiteral("chat.add"),
        QJsonObject{{QStringLiteral("chat_id"), chatId},
                    {QStringLiteral("user_ids"), QJsonArray{thirdId}}});
    QVERIFY(!forbidden.value(QStringLiteral("ok")).toBool(false));

    // Владелец добавляет третьего — тот получает ev.chat.
    const QJsonObject addReply = request(
        first, QStringLiteral("chat.add"),
        QJsonObject{{QStringLiteral("chat_id"), chatId},
                    {QStringLiteral("user_ids"), QJsonArray{thirdId}}},
        &ok);
    QVERIFY(ok);
    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(thirdEvents, QStringLiteral("ev.chat")), 10000);

    // Выход участника: остаётся двое.
    const QJsonObject leaveReply = request(
        second, QStringLiteral("chat.leave"),
        QJsonObject{{QStringLiteral("chat_id"), chatId}}, &ok);
    QVERIFY(ok);

    const QJsonObject afterLeave = request(
        first, QStringLiteral("chat.members"),
        QJsonObject{{QStringLiteral("chat_id"), chatId}}, &ok);
    QVERIFY(ok);
    QCOMPARE(afterLeave.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("members")).toArray().size(),
             2);

    first.disconnectFromServer();
    second.disconnectFromServer();
    third.disconnectFromServer();
}

QTEST_GUILESS_MAIN(TestGroupChat)
#include "tst_group_chat.moc"
