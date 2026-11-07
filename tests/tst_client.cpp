#include "core/Protocol.h"
#include "net/ClientConnection.h"
#include "net/RequestTracker.h"
#include "server/ServerApp.h"

#include <QJsonObject>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestClientNetwork : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void trackerTimesOut();
    void trackerCompletesOnce();
    void trackerFailAllReportsOffline();

    void connectsAndExchangesHello();
    void refusesRequestWithoutAuth();
    void offlineRequestFailsImmediately();
    void schedulesReconnectWhenServerUnreachable();

private:
    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<server::ServerApp> m_server;
};

void TestClientNetwork::init()
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

void TestClientNetwork::cleanup()
{
    m_server.reset();
    m_dir.reset();
}

void TestClientNetwork::trackerTimesOut()
{
    net::RequestTracker tracker;
    int calls = 0;
    core::ErrorCode lastCode = core::ErrorCode::None;

    tracker.track(QStringLiteral("req-1"),
                  [&](const QJsonObject &reply) {
                      ++calls;
                      lastCode = core::proto::errorCodeOf(reply);
                  },
                  30);

    QTRY_VERIFY_WITH_TIMEOUT(calls == 1, 2000);
    QCOMPARE(lastCode, core::ErrorCode::Timeout);
    QCOMPARE(tracker.pendingCount(), 0);

    QTest::qWait(80);
    QCOMPARE(calls, 1); // таймаут не срабатывает повторно
}

void TestClientNetwork::trackerCompletesOnce()
{
    net::RequestTracker tracker;
    int calls = 0;
    QJsonObject received;

    tracker.track(QStringLiteral("req-1"),
                  [&](const QJsonObject &reply) {
                      ++calls;
                      received = reply;
                  },
                  1000);

    const QJsonObject reply = core::proto::successReply(QStringLiteral("req-1"));
    QVERIFY(tracker.complete(QStringLiteral("req-1"), reply));
    QVERIFY(!tracker.complete(QStringLiteral("req-1"), reply));

    QCOMPARE(calls, 1);
    QCOMPARE(received.value(QStringLiteral("ok")).toBool(), true);

    QTest::qWait(60);
    QCOMPARE(calls, 1);
}

void TestClientNetwork::trackerFailAllReportsOffline()
{
    net::RequestTracker tracker;
    QList<core::ErrorCode> codes;

    for (int i = 0; i < 3; ++i) {
        tracker.track(QStringLiteral("req-%1").arg(i),
                      [&](const QJsonObject &reply) { codes.append(core::proto::errorCodeOf(reply)); },
                      5000);
    }

    QCOMPARE(tracker.pendingCount(), 3);
    tracker.failAll(core::ErrorCode::Offline);

    QCOMPARE(codes.size(), 3);
    for (core::ErrorCode code : qAsConst(codes))
        QCOMPARE(code, core::ErrorCode::Offline);
    QCOMPARE(tracker.pendingCount(), 0);
}

void TestClientNetwork::connectsAndExchangesHello()
{
    net::ClientConnection connection;
    connection.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    QSignalSpy connectedSpy(&connection, &net::ClientConnection::socketConnected);
    connection.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 1, 5000);
    QCOMPARE(connection.state(), net::ClientConnection::State::Connected);

    bool gotReply = false;
    QJsonObject reply;
    QJsonObject helloData;
    helloData.insert(QStringLiteral("app"), QStringLiteral("Test/1.0"));
    helloData.insert(QStringLiteral("proto"), 1);

    connection.request(QStringLiteral("hello"), helloData, [&](const QJsonObject &object) {
        gotReply = true;
        reply = object;
    });

    QTRY_VERIFY_WITH_TIMEOUT(gotReply, 5000);
    QCOMPARE(core::proto::errorCodeOf(reply), core::ErrorCode::None);
    QCOMPARE(reply.value(QStringLiteral("data")).toObject().value(QStringLiteral("proto")).toInt(),
             1);
}

void TestClientNetwork::refusesRequestWithoutAuth()
{
    net::ClientConnection connection;
    connection.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    QSignalSpy connectedSpy(&connection, &net::ClientConnection::socketConnected);
    connection.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 1, 5000);

    bool gotReply = false;
    core::ErrorCode code = core::ErrorCode::None;
    connection.request(QStringLiteral("chat.list"), QJsonObject(), [&](const QJsonObject &reply) {
        gotReply = true;
        code = core::proto::errorCodeOf(reply);
    });

    QTRY_VERIFY_WITH_TIMEOUT(gotReply, 5000);
    QCOMPARE(code, core::ErrorCode::AuthRequired);
}

void TestClientNetwork::offlineRequestFailsImmediately()
{
    net::ClientConnection connection;
    connection.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    bool gotReply = false;
    core::ErrorCode code = core::ErrorCode::None;

    connection.request(QStringLiteral("ping"), QJsonObject(), [&](const QJsonObject &reply) {
        gotReply = true;
        code = core::proto::errorCodeOf(reply);
    });

    QTRY_VERIFY_WITH_TIMEOUT(gotReply, 1000);
    QCOMPARE(code, core::ErrorCode::Offline);
}

void TestClientNetwork::schedulesReconnectWhenServerUnreachable()
{
    net::ClientConnection connection;
    connection.setEndpoint(QStringLiteral("127.0.0.1"), 1); // заведомо закрытый порт

    QSignalSpy reconnectSpy(&connection, &net::ClientConnection::reconnectScheduled);
    connection.connectToServer();

    QTRY_VERIFY_WITH_TIMEOUT(reconnectSpy.count() >= 1, 5000);
    QVERIFY(reconnectSpy.first().at(0).toInt() >= 1000);

    connection.disconnectFromServer();
    QCOMPARE(connection.state(), net::ClientConnection::State::Disconnected);
}

QTEST_GUILESS_MAIN(TestClientNetwork)
#include "tst_client.moc"
