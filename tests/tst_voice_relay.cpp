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

// Голосовой релей: кадры одного участника доходят до второго,
// после выхода — нет.
class TestVoiceRelay : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void framesAreRelayedBetweenParticipants();

private:
    QJsonObject request(net::ClientConnection &connection, const QString &type,
                        const QJsonObject &data, bool *ok = nullptr);
    void registerUser(net::ClientConnection &connection, const QString &username);

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<server::ServerApp> m_server;
};

void TestVoiceRelay::init()
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

void TestVoiceRelay::cleanup()
{
    m_server.reset();
    m_dir.reset();
}

QJsonObject TestVoiceRelay::request(net::ClientConnection &connection, const QString &type,
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

void TestVoiceRelay::registerUser(net::ClientConnection &connection, const QString &username)
{
    bool ok = false;
    request(connection, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), username},
                        {QStringLiteral("display_name"), username},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);
}

void TestVoiceRelay::framesAreRelayedBetweenParticipants()
{
    net::ClientConnection first;
    net::ClientConnection second;
    first.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    second.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    QSignalSpy firstConnected(&first, &net::ClientConnection::socketConnected);
    QSignalSpy secondConnected(&second, &net::ClientConnection::socketConnected);
    QSignalSpy secondFrames(&second, &net::ClientConnection::voiceFrameReceived);
    QSignalSpy firstFrames(&first, &net::ClientConnection::voiceFrameReceived);
    QSignalSpy secondEvents(&second, &net::ClientConnection::eventReceived);

    first.connectToServer();
    second.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(firstConnected.count(), 1, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(secondConnected.count(), 1, 15000);

    registerUser(first, QStringLiteral("voice_a"));
    registerUser(second, QStringLiteral("voice_b"));

    bool ok = false;
    const QJsonObject chatsReply = request(first, QStringLiteral("chat.list"), QJsonObject(), &ok);
    QVERIFY(ok);
    const QJsonArray chats = chatsReply.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("chats")).toArray();
    QVERIFY(!chats.isEmpty());
    const double chatId = chats.first().toObject().value(QStringLiteral("id")).toDouble();

    const QJsonObject joinReply = request(
        first, QStringLiteral("voice.join"),
        QJsonObject{{QStringLiteral("chat_id"), chatId}}, &ok);
    QVERIFY(ok);
    QCOMPARE(joinReply.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("codec")).toString(),
             QStringLiteral("opus"));

    const QJsonObject secondJoin = request(
        second, QStringLiteral("voice.join"),
        QJsonObject{{QStringLiteral("chat_id"), chatId}}, &ok);
    QVERIFY(ok);

    QTRY_VERIFY_WITH_TIMEOUT(secondEvents.count() > 0, 5000);

    const QByteArray opusPayload = QByteArray::fromHex("f8fffe0102030405");
    QVERIFY(first.sendVoiceFrame(static_cast<quint32>(chatId), 42, opusPayload));

    QTRY_COMPARE_WITH_TIMEOUT(secondFrames.count(), 1, 10000);
    const QList<QVariant> frame = secondFrames.first();
    QCOMPARE(frame.at(0).toUInt(), static_cast<quint32>(chatId));
    QVERIFY(frame.at(1).toUInt() > 0);
    QCOMPARE(frame.at(2).toUInt(), 42u);
    QCOMPARE(frame.at(3).toByteArray(), opusPayload);
    QCOMPARE(firstFrames.count(), 0);

    // После выхода второй не получает кадры.
    request(second, QStringLiteral("voice.leave"),
            QJsonObject{{QStringLiteral("chat_id"), chatId}}, &ok);
    QVERIFY(ok);
    QTest::qWait(200);

    const int framesBefore = secondFrames.count();
    QVERIFY(first.sendVoiceFrame(static_cast<quint32>(chatId), 43, opusPayload));
    QTest::qWait(500);
    QCOMPARE(secondFrames.count(), framesBefore);

    first.disconnectFromServer();
    second.disconnectFromServer();
}

QTEST_GUILESS_MAIN(TestVoiceRelay)
#include "tst_voice_relay.moc"
