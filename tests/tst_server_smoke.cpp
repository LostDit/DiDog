#include "core/FrameCodec.h"
#include "core/Protocol.h"
#include "server/ServerApp.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QScopedPointer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

// Простой клиент поверх протокола: синхронные вызовы с прокачкой цикла событий
// (сервер живёт в этом же потоке, поэтому QTest::qWait обязателен).
class TestLink
{
public:
    bool connectTo(quint16 port, int timeoutMs = 5000)
    {
        socket.connectToHost(QHostAddress::LocalHost, port);

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (socket.state() == QAbstractSocket::ConnectedState)
                return true;
            if (socket.state() == QAbstractSocket::UnconnectedState && timer.elapsed() > 200)
                return false;
            QTest::qWait(10);
        }
        return false;
    }

    QJsonObject call(const QString &type, const QJsonObject &data, bool *ok = nullptr,
                     int timeoutMs = 10000)
    {
        const QJsonObject request = core::proto::request(type, data);
        const QString requestId = request.value(QStringLiteral("id")).toString();

        const QByteArray frame = core::FrameCodec::encode(core::FrameType::Json,
                                                          core::proto::encode(request));
        socket.write(frame);
        socket.flush();

        QJsonObject reply;
        const bool received = waitForReply(requestId, &reply, timeoutMs);
        if (ok)
            *ok = received && reply.value(QStringLiteral("ok")).toBool(false);
        return reply;
    }

    // Дочитывает события, пришедшие без запроса.
    void pump(int milliseconds)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < milliseconds) {
            QTest::qWait(10);
            drain();
        }
    }

    QList<QJsonObject> events;
    QHash<quint32, QByteArray> chunks;
    QSet<quint32> finishedTransfers;

    void sendChunk(quint32 transferId, quint32 sequence, const QByteArray &data)
    {
        const QByteArray chunk = core::encodeChunk(transferId, sequence, data);
        socket.write(core::FrameCodec::encode(core::FrameType::Binary, chunk));
        socket.flush();
    }

private:
    void drain()
    {
        codec.append(socket.readAll());

        core::Frame frame;
        QString error;
        for (;;) {
            const auto status = codec.next(&frame, &error);
            if (status == core::FrameCodec::Status::NeedMore)
                return;
            if (status == core::FrameCodec::Status::Error)
                return;

            if (frame.type == core::FrameType::Binary) {
                quint32 transferId = 0;
                quint32 sequence = 0;
                QByteArray data;
                if (core::decodeChunk(frame.payload, &transferId, &sequence, &data))
                    chunks[transferId].append(data);
                continue;
            }

            QJsonObject object;
            if (!core::proto::decode(frame.payload, &object, &error))
                return;

            if (object.value(QStringLiteral("type")).toString() == QLatin1String("attach.end")) {
                const quint32 transferId = static_cast<quint32>(
                    object.value(QStringLiteral("data")).toObject()
                        .value(QStringLiteral("transfer_id")).toDouble());
                finishedTransfers.insert(transferId);
                continue;
            }

            if (object.value(QStringLiteral("type")).toString() == QLatin1String("ev"))
                events.append(object);
        }
    }

    bool waitForReply(const QString &requestId, QJsonObject *reply, int timeoutMs)
    {
        QElapsedTimer timer;
        timer.start();

        while (timer.elapsed() < timeoutMs) {
            QTest::qWait(5);

            if (socket.state() != QAbstractSocket::ConnectedState)
                return false;

            codec.append(socket.readAll());

            core::Frame frame;
            QString error;
            for (;;) {
                const auto status = codec.next(&frame, &error);
                if (status == core::FrameCodec::Status::NeedMore)
                    break;
                if (status == core::FrameCodec::Status::Error)
                    return false;

                QJsonObject object;
                if (frame.type == core::FrameType::Binary) {
                    quint32 transferId = 0;
                    quint32 sequence = 0;
                    QByteArray data;
                    if (!core::decodeChunk(frame.payload, &transferId, &sequence, &data))
                        return false;
                    chunks[transferId].append(data);
                    continue;
                }

                if (!core::proto::decode(frame.payload, &object, &error))
                    return false;

                if (object.value(QStringLiteral("type")).toString()
                    == QLatin1String("attach.end")) {
                    const quint32 transferId = static_cast<quint32>(
                        object.value(QStringLiteral("data")).toObject()
                            .value(QStringLiteral("transfer_id")).toDouble());
                    finishedTransfers.insert(transferId);
                    continue;
                }

                if (object.value(QStringLiteral("type")).toString() == QLatin1String("ev")) {
                    events.append(object);
                    continue;
                }
                if (object.value(QStringLiteral("reply_to")).toString() == requestId) {
                    *reply = object;
                    return true;
                }
            }
        }
        return false;
    }

public:
    QTcpSocket socket;
    core::FrameCodec codec;
};

class TestServerSmoke : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void rejectsRequestWithoutAuth();
    void registersAndRejectsDuplicates();
    void rejectsWrongPassword();
    void exchangesMessagesBetweenTwoClients();
    void transfersAttachmentEndToEnd();

private:
    QJsonObject registerUser(TestLink &link, const QString &username,
                             const QString &displayName);

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<server::ServerApp> m_server;
};

void TestServerSmoke::init()
{
    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());

    m_server.reset(new server::ServerApp);
    QString error;
    QVERIFY2(m_server->initialize(m_dir->filePath(QStringLiteral("server.db")),
                                  m_dir->filePath(QStringLiteral("attachments")), &error),
             qPrintable(error));
    QVERIFY2(m_server->start(0), qPrintable(m_server->errorString()));
    QVERIFY(m_server->port() > 0);
}

void TestServerSmoke::cleanup()
{
    m_server.reset();
    m_dir.reset();
}

QJsonObject TestServerSmoke::registerUser(TestLink &link, const QString &username,
                                          const QString &displayName)
{
    QJsonObject data;
    data.insert(QStringLiteral("username"), username);
    data.insert(QStringLiteral("display_name"), displayName);
    data.insert(QStringLiteral("password"), QStringLiteral("password123"));
    return link.call(QStringLiteral("auth.register"), data);
}

void TestServerSmoke::rejectsRequestWithoutAuth()
{
    TestLink link;
    QVERIFY(link.connectTo(m_server->port()));

    bool ok = true;
    const QJsonObject reply = link.call(QStringLiteral("chat.list"), QJsonObject(), &ok);
    QVERIFY(!ok);
    QCOMPARE(core::proto::errorCodeOf(reply), core::ErrorCode::AuthRequired);
}

void TestServerSmoke::registersAndRejectsDuplicates()
{
    TestLink link;
    QVERIFY(link.connectTo(m_server->port()));

    bool ok = false;
    const QJsonObject hello = link.call(QStringLiteral("hello"),
                                        QJsonObject{{QStringLiteral("app"),
                                                     QStringLiteral("Test/1.0")},
                                                    {QStringLiteral("proto"), 1}},
                                        &ok);
    QVERIFY2(ok, qPrintable(core::proto::errorMessageOf(hello)));

    const QJsonObject registered = registerUser(link, QStringLiteral("alice"),
                                                QStringLiteral("Алиса"));
    QVERIFY2(registered.value(QStringLiteral("ok")).toBool(),
             qPrintable(core::proto::errorMessageOf(registered)));
    QVERIFY(!registered.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("token")).toString().isEmpty());

    TestLink second;
    QVERIFY(second.connectTo(m_server->port()));
    const QJsonObject duplicate = registerUser(second, QStringLiteral("ALICE"),
                                               QStringLiteral("Другая Алиса"));
    QCOMPARE(core::proto::errorCodeOf(duplicate), core::ErrorCode::UserExists);
}

void TestServerSmoke::rejectsWrongPassword()
{
    TestLink link;
    QVERIFY(link.connectTo(m_server->port()));
    QVERIFY(registerUser(link, QStringLiteral("bob"), QStringLiteral("Боб"))
                .value(QStringLiteral("ok")).toBool());

    TestLink other;
    QVERIFY(other.connectTo(m_server->port()));

    QJsonObject data;
    data.insert(QStringLiteral("username"), QStringLiteral("bob"));
    data.insert(QStringLiteral("password"), QStringLiteral("wrong-password"));

    bool ok = true;
    const QJsonObject reply = other.call(QStringLiteral("auth.login"), data, &ok);
    QVERIFY(!ok);
    QCOMPARE(core::proto::errorCodeOf(reply), core::ErrorCode::AuthFailed);
}

void TestServerSmoke::exchangesMessagesBetweenTwoClients()
{
    TestLink alice;
    TestLink bob;
    QVERIFY(alice.connectTo(m_server->port()));
    QVERIFY(bob.connectTo(m_server->port()));

    QVERIFY(registerUser(alice, QStringLiteral("alice"), QStringLiteral("Алиса"))
                .value(QStringLiteral("ok")).toBool());
    QVERIFY(registerUser(bob, QStringLiteral("bob"), QStringLiteral("Боб"))
                .value(QStringLiteral("ok")).toBool());

    bool ok = false;

    // Боб открывает личный чат с Алисой.
    QJsonObject directData;
    directData.insert(QStringLiteral("username"), QStringLiteral("alice"));
    const QJsonObject direct = bob.call(QStringLiteral("chat.direct"), directData, &ok);
    QVERIFY2(ok, qPrintable(core::proto::errorMessageOf(direct)));
    const qint64 chatId = static_cast<qint64>(direct.value(QStringLiteral("data")).toObject()
                                                  .value(QStringLiteral("chat")).toObject()
                                                  .value(QStringLiteral("id")).toDouble());
    QVERIFY(chatId > 0);

    // Алиса должна увидеть новый чат в списке.
    const QJsonObject list = alice.call(QStringLiteral("chat.list"), QJsonObject(), &ok);
    QVERIFY2(ok, qPrintable(core::proto::errorMessageOf(list)));
    const QJsonArray chats = list.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("chats")).toArray();
    QVERIFY(chats.size() >= 1);

    // Сообщение от Алисы.
    QJsonObject messageData;
    messageData.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    messageData.insert(QStringLiteral("client_msg_id"), QStringLiteral("client-msg-1"));
    messageData.insert(QStringLiteral("kind"), QStringLiteral("text"));
    messageData.insert(QStringLiteral("body"), QStringLiteral("Привет, Боб!"));

    const QJsonObject ack = alice.call(QStringLiteral("msg.send"), messageData, &ok);
    QVERIFY2(ok, qPrintable(core::proto::errorMessageOf(ack)));

    const QJsonObject ackData = ack.value(QStringLiteral("data")).toObject();
    const qint64 messageId = static_cast<qint64>(ackData.value(QStringLiteral("message_id")).toDouble());
    QCOMPARE(static_cast<qint64>(ackData.value(QStringLiteral("seq")).toDouble()), qint64(1));
    QVERIFY(messageId > 0);

    // Боб получает событие с тем же текстом.
    bob.pump(300);
    bool sawMessage = false;
    for (const QJsonObject &event : qAsConst(bob.events)) {
        if (event.value(QStringLiteral("event")).toString() != QLatin1String("ev.msg"))
            continue;
        const QJsonObject message = event.value(QStringLiteral("data")).toObject()
                                        .value(QStringLiteral("message")).toObject();
        if (message.value(QStringLiteral("body")).toString() == QStringLiteral("Привет, Боб!"))
            sawMessage = true;
    }
    QVERIFY2(sawMessage, "Боб не получил ev.msg");

    // Повторная отправка с тем же client_msg_id не создаёт дубликат.
    const QJsonObject repeated = alice.call(QStringLiteral("msg.send"), messageData, &ok);
    QVERIFY(ok);
    QCOMPARE(static_cast<qint64>(repeated.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("message_id")).toDouble()),
             messageId);

    // Прочтение: Алиса получает ev.read.
    alice.events.clear();
    QJsonObject readData;
    readData.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    readData.insert(QStringLiteral("up_to_seq"), 1);
    QVERIFY(bob.call(QStringLiteral("chat.read"), readData, &ok).value(QStringLiteral("ok")).toBool());

    alice.pump(300);
    bool sawRead = false;
    for (const QJsonObject &event : qAsConst(alice.events)) {
        if (event.value(QStringLiteral("event")).toString() == QLatin1String("ev.read"))
            sawRead = true;
    }
    QVERIFY2(sawRead, "Алиса не получила ev.read");

    // Квитанция delivered доходит до отправителя.
    alice.events.clear();
    QJsonObject receiptData;
    receiptData.insert(QStringLiteral("message_ids"),
                       QJsonArray{static_cast<double>(messageId)});
    QVERIFY(bob.call(QStringLiteral("receipt.delivered"), receiptData, &ok)
                .value(QStringLiteral("ok")).toBool());

    alice.pump(300);
    bool sawReceipt = false;
    for (const QJsonObject &event : qAsConst(alice.events)) {
        if (event.value(QStringLiteral("event")).toString() != QLatin1String("ev.receipt"))
            continue;
        if (event.value(QStringLiteral("data")).toObject()
                .value(QStringLiteral("state")).toString() == QLatin1String("delivered")) {
            sawReceipt = true;
        }
    }
    QVERIFY2(sawReceipt, "Алиса не получила ev.receipt");
}

void TestServerSmoke::transfersAttachmentEndToEnd()
{
    TestLink alice;
    TestLink bob;
    QVERIFY(alice.connectTo(m_server->port()));
    QVERIFY(bob.connectTo(m_server->port()));

    QVERIFY(registerUser(alice, QStringLiteral("alice"), QStringLiteral("Алиса"))
                .value(QStringLiteral("ok")).toBool());
    QVERIFY(registerUser(bob, QStringLiteral("bob"), QStringLiteral("Боб"))
                .value(QStringLiteral("ok")).toBool());

    bool ok = false;
    QJsonObject directData;
    directData.insert(QStringLiteral("username"), QStringLiteral("alice"));
    const QJsonObject direct = bob.call(QStringLiteral("chat.direct"), directData, &ok);
    QVERIFY2(ok, qPrintable(core::proto::errorMessageOf(direct)));
    const qint64 chatId = static_cast<qint64>(direct.value(QStringLiteral("data")).toObject()
                                                  .value(QStringLiteral("chat")).toObject()
                                                  .value(QStringLiteral("id")).toDouble());

    QByteArray payload(100 * 1024, Qt::Uninitialized);
    for (int i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<char>((i * 7 + 13) % 251);

    const QString sha = QString::fromLatin1(
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());

    QJsonObject begin;
    begin.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    begin.insert(QStringLiteral("file_name"), QStringLiteral("photo.bin"));
    begin.insert(QStringLiteral("mime"), QStringLiteral("application/octet-stream"));
    begin.insert(QStringLiteral("byte_size"), static_cast<double>(payload.size()));
    begin.insert(QStringLiteral("sha256"), sha);

    const QJsonObject beginReply = alice.call(QStringLiteral("attach.begin"), begin, &ok);
    QVERIFY2(ok, qPrintable(core::proto::errorMessageOf(beginReply)));

    const quint32 transferId = static_cast<quint32>(
        beginReply.value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("transfer_id")).toDouble());
    QVERIFY(transferId > 0);

    constexpr int chunkSize = 64 * 1024;
    quint32 sequence = 0;
    for (int offset = 0; offset < payload.size(); offset += chunkSize)
        alice.sendChunk(transferId, sequence++, payload.mid(offset, chunkSize));
    alice.pump(100);

    QJsonObject finish;
    finish.insert(QStringLiteral("transfer_id"), static_cast<double>(transferId));
    const QJsonObject finishReply = alice.call(QStringLiteral("attach.finish"), finish, &ok);
    QVERIFY2(ok, qPrintable(core::proto::errorMessageOf(finishReply)));

    const qint64 attachmentId = static_cast<qint64>(
        finishReply.value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("attachment_id")).toDouble());
    QVERIFY(attachmentId > 0);

    QJsonObject send;
    send.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    send.insert(QStringLiteral("client_msg_id"), QStringLiteral("attach-1"));
    send.insert(QStringLiteral("kind"), QStringLiteral("file"));
    send.insert(QStringLiteral("body"), QString());
    send.insert(QStringLiteral("attachment_id"), static_cast<double>(attachmentId));

    const QJsonObject ack = alice.call(QStringLiteral("msg.send"), send, &ok);
    QVERIFY2(ok, qPrintable(core::proto::errorMessageOf(ack)));

    // Боб получает сообщение с метаданными вложения.
    bob.pump(400);
    qint64 receivedAttachmentId = 0;
    for (const QJsonObject &event : qAsConst(bob.events)) {
        if (event.value(QStringLiteral("event")).toString() != QLatin1String("ev.msg"))
            continue;
        const QJsonObject message = event.value(QStringLiteral("data")).toObject()
                                        .value(QStringLiteral("message")).toObject();
        const QJsonObject attachment = message.value(QStringLiteral("attachment")).toObject();
        if (!attachment.isEmpty())
            receivedAttachmentId = static_cast<qint64>(
                attachment.value(QStringLiteral("id")).toDouble());
    }
    QVERIFY2(receivedAttachmentId > 0, "Боб не получил метаданные вложения");

    // Скачивание.
    QJsonObject get;
    get.insert(QStringLiteral("attachment_id"), static_cast<double>(receivedAttachmentId));
    const QJsonObject getReply = bob.call(QStringLiteral("attach.get"), get, &ok);
    QVERIFY2(ok, qPrintable(core::proto::errorMessageOf(getReply)));

    const quint32 downloadId = static_cast<quint32>(
        getReply.value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("transfer_id")).toDouble());

    bob.pump(600);
    QCOMPARE(bob.chunks.value(downloadId).size(), payload.size());
    QCOMPARE(bob.chunks.value(downloadId), payload);
    QVERIFY(bob.finishedTransfers.contains(downloadId));

    const QString downloadedSha = QString::fromLatin1(
        QCryptographicHash::hash(bob.chunks.value(downloadId), QCryptographicHash::Sha256).toHex());
    QCOMPARE(downloadedSha, sha);
}

QTEST_GUILESS_MAIN(TestServerSmoke)
#include "tst_server_smoke.moc"
