#include "core/Protocol.h"
#include "net/ClientConnection.h"
#include "server/ServerApp.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QNetworkDatagram>
#include <QUdpSocket>
#include <QtTest>

// UDP-релей: дейтаграмма первого участника приходит второму с его user_id.
class TestVoiceUdp : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void datagramsAreRelayed();

private:
    QJsonObject request(net::ClientConnection &connection, const QString &type,
                        const QJsonObject &data, bool *ok = nullptr);
    QJsonObject registerUser(net::ClientConnection &connection, const QString &username);

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<server::ServerApp> m_server;
};

void TestVoiceUdp::init()
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

void TestVoiceUdp::cleanup()
{
    m_server.reset();
    m_dir.reset();
}

QJsonObject TestVoiceUdp::request(net::ClientConnection &connection, const QString &type,
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

QJsonObject TestVoiceUdp::registerUser(net::ClientConnection &connection,
                                       const QString &username)
{
    bool ok = false;
    const QJsonObject reply = request(
        connection, QStringLiteral("auth.register"),
        QJsonObject{{QStringLiteral("username"), username},
                    {QStringLiteral("display_name"), username},
                    {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
        &ok);
    if (!ok)
        return QJsonObject();
    return reply.value(QStringLiteral("data")).toObject()
        .value(QStringLiteral("user")).toObject();
}

void TestVoiceUdp::datagramsAreRelayed()
{
    net::ClientConnection first;
    net::ClientConnection second;
    first.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    second.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    QSignalSpy firstConnected(&first, &net::ClientConnection::socketConnected);
    QSignalSpy secondConnected(&second, &net::ClientConnection::socketConnected);
    first.connectToServer();
    second.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(firstConnected.count(), 1, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(secondConnected.count(), 1, 15000);

    const QJsonObject firstUser = registerUser(first, QStringLiteral("udp_a"));
    registerUser(second, QStringLiteral("udp_b"));
    const double firstUserId = firstUser.value(QStringLiteral("id")).toDouble();

    bool ok = false;
    const QJsonObject chatsReply = request(first, QStringLiteral("chat.list"), QJsonObject(), &ok);
    QVERIFY(ok);
    const double chatId = chatsReply.value(QStringLiteral("data")).toObject()
                              .value(QStringLiteral("chats")).toArray()
                              .first().toObject().value(QStringLiteral("id")).toDouble();

    const QJsonObject firstJoin = request(
        first, QStringLiteral("voice.join"),
        QJsonObject{{QStringLiteral("chat_id"), chatId}}, &ok);
    QVERIFY(ok);
    const int udpPort = firstJoin.value(QStringLiteral("data")).toObject()
                            .value(QStringLiteral("udp_port")).toInt();
    const quint32 firstToken = static_cast<quint32>(firstJoin.value(QStringLiteral("data"))
                                                        .toObject()
                                                        .value(QStringLiteral("udp_token"))
                                                        .toDouble());
    QVERIFY(udpPort > 0);
    QVERIFY(firstToken != 0);

    const QByteArray firstKey = QByteArray::fromHex(
        firstJoin.value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("voice_key")).toString().toLatin1());
    QCOMPARE(firstKey.size(), 32);

    const QJsonObject secondJoin = request(
        second, QStringLiteral("voice.join"),
        QJsonObject{{QStringLiteral("chat_id"), chatId}}, &ok);
    QVERIFY(ok);
    const quint32 secondToken = static_cast<quint32>(secondJoin.value(QStringLiteral("data"))
                                                         .toObject()
                                                         .value(QStringLiteral("udp_token"))
                                                         .toDouble());

    const QByteArray secondKey = QByteArray::fromHex(
        secondJoin.value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("voice_key")).toString().toLatin1());
    QCOMPARE(secondKey, firstKey); // одна комната — один ключ шифрования
    QVERIFY(secondToken != 0);

    QUdpSocket firstSocket;
    QVERIFY(firstSocket.bind(QHostAddress(QHostAddress::LocalHost), 0));
    QUdpSocket secondSocket;
    QVERIFY(secondSocket.bind(QHostAddress(QHostAddress::LocalHost), 0));

    const QByteArray payload = QByteArray::fromHex("f8ff0102");

    // «Привет» от второго участника: сервер запоминает его UDP-адрес.
    const QByteArray hello = core::proto::encodeVoiceDatagram(secondToken, 0, QByteArray());
    secondSocket.writeDatagram(hello, QHostAddress::LocalHost, udpPort);
    QTest::qWait(200);

    const QByteArray datagram = core::proto::encodeVoiceDatagram(firstToken, 5, payload);
    QCOMPARE(firstSocket.writeDatagram(datagram, QHostAddress::LocalHost, udpPort),
             qint64(datagram.size()));

    QTRY_VERIFY_WITH_TIMEOUT(secondSocket.hasPendingDatagrams(), 5000);

    const QNetworkDatagram relayed = secondSocket.receiveDatagram();
    quint32 userId = 0;
    quint32 sequence = 0;
    QByteArray received;
    QVERIFY(core::proto::decodeVoiceRelay(relayed.data(), &userId, &sequence, &received));
    QCOMPARE(static_cast<double>(userId), firstUserId);
    QCOMPARE(sequence, 5u);
    QCOMPARE(received, payload);

    // Отправитель не получает собственный кадр.
    QTest::qWait(200);
    QCOMPARE(firstSocket.hasPendingDatagrams(), false);

    first.disconnectFromServer();
    second.disconnectFromServer();
}

QTEST_GUILESS_MAIN(TestVoiceUdp)
#include "tst_voice_udp.moc"
