#include "core/Protocol.h"
#include "net/ClientConnection.h"
#include "server/ServerApp.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
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

QJsonObject eventData(const QSignalSpy &spy, const QString &name)
{
    for (const QList<QVariant> &arguments : spy) {
        if (arguments.value(0).toString() == name)
            return arguments.value(1).value<QJsonObject>();
    }
    return QJsonObject();
}

} // namespace

// Редактирование и удаление сообщений на уровне протокола: оба клиента
// видят изменения, чужие сообщения менять нельзя.
class TestMessageOps : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void editAndDeleteReachBothSides();
    void foreignMessageCannotBeChanged();
    void replyIsStoredAndValidated();
    void reactionsReachBothSides();
    void forwardCopiesTextToAnotherChat();
    void pinnedMessageAppearsInChat();
    void chatFlagsArePersisted();
    void voicePubkeysAreRelayed();

private:
    QJsonObject request(net::ClientConnection &connection, const QString &type,
                        const QJsonObject &data, bool *ok = nullptr);

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<server::ServerApp> m_server;
};

void TestMessageOps::init()
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

void TestMessageOps::cleanup()
{
    m_server.reset();
    m_dir.reset();
}

QJsonObject TestMessageOps::request(net::ClientConnection &connection, const QString &type,
                                    const QJsonObject &data, bool *ok)
{
    bool done = false;
    QJsonObject reply;

    connection.request(type, data, [&done, &reply](const QJsonObject &value) {
        reply = value;
        done = true;
    });

    if (ok)
        *ok = false;

    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < 10000)
        QTest::qWait(10);

    if (ok)
        *ok = done && reply.value(QStringLiteral("ok")).toBool(false);

    return reply;
}

void TestMessageOps::editAndDeleteReachBothSides()
{
    net::ClientConnection first;
    net::ClientConnection second;
    first.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    second.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    QSignalSpy firstConnected(&first, &net::ClientConnection::socketConnected);
    QSignalSpy secondConnected(&second, &net::ClientConnection::socketConnected);
    QSignalSpy secondEvents(&second, &net::ClientConnection::eventReceived);

    first.connectToServer();
    second.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(firstConnected.count(), 1, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(secondConnected.count(), 1, 15000);

    bool ok = false;
    request(first, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("author")},
                        {QStringLiteral("display_name"), QStringLiteral("Author")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);

    request(second, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("reader")},
                        {QStringLiteral("display_name"), QStringLiteral("Reader")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);

    const QJsonObject chatsReply = request(first, QStringLiteral("chat.list"), QJsonObject(), &ok);
    QVERIFY(ok);
    const QJsonArray chats = chatsReply.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("chats")).toArray();
    QVERIFY(!chats.isEmpty());
    const double chatId = chats.first().toObject().value(QStringLiteral("id")).toDouble();

    const QJsonObject sendReply = request(
        first, QStringLiteral("msg.send"),
        QJsonObject{{QStringLiteral("chat_id"), chatId},
                    {QStringLiteral("client_msg_id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                    {QStringLiteral("kind"), QStringLiteral("text")},
                    {QStringLiteral("body"), QStringLiteral("исходный текст")}},
        &ok);
    QVERIFY(ok);
    const double messageId = sendReply.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("message_id")).toDouble();
    QVERIFY(messageId > 0);

    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(secondEvents, QStringLiteral("ev.msg")), 10000);

    // Редактирование: второй клиент получает событие с новым текстом.
    request(first, QStringLiteral("msg.edit"),
            QJsonObject{{QStringLiteral("chat_id"), chatId},
                        {QStringLiteral("message_id"), messageId},
                        {QStringLiteral("body"), QStringLiteral("изменённый текст")}},
            &ok);
    QVERIFY(ok);

    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(secondEvents, QStringLiteral("ev.msg.edited")), 10000);
    const QJsonObject edited = eventData(secondEvents, QStringLiteral("ev.msg.edited"))
                                   .value(QStringLiteral("message")).toObject();
    QCOMPARE(edited.value(QStringLiteral("body")).toString(), QStringLiteral("изменённый текст"));
    QVERIFY(edited.value(QStringLiteral("edited_at")).toDouble() > 0);

    // Удаление у всех: приходит событие с deleted = true.
    request(first, QStringLiteral("msg.delete"),
            QJsonObject{{QStringLiteral("chat_id"), chatId},
                        {QStringLiteral("message_id"), messageId}},
            &ok);
    QVERIFY(ok);

    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(secondEvents, QStringLiteral("ev.msg.deleted")), 10000);
    const QJsonObject deleted = eventData(secondEvents, QStringLiteral("ev.msg.deleted"))
                                    .value(QStringLiteral("message")).toObject();
    QVERIFY(deleted.value(QStringLiteral("deleted")).toBool());
    QVERIFY(deleted.value(QStringLiteral("body")).toString().isEmpty());

    // История отдаёт удалённое сообщение как tombstone.
    const QJsonObject openReply = request(second, QStringLiteral("chat.open"),
                                          QJsonObject{{QStringLiteral("chat_id"), chatId},
                                                      {QStringLiteral("limit"), 10}},
                                          &ok);
    QVERIFY(ok);
    const QJsonArray messages = openReply.value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("messages")).toArray();
    bool foundDeleted = false;
    for (const QJsonValue &value : messages) {
        const QJsonObject message = value.toObject();
        if (static_cast<qint64>(message.value(QStringLiteral("id")).toDouble()) == messageId)
            foundDeleted = message.value(QStringLiteral("deleted")).toBool();
    }
    QVERIFY(foundDeleted);

    first.disconnectFromServer();
    second.disconnectFromServer();
}

void TestMessageOps::replyIsStoredAndValidated()
{
    net::ClientConnection client;
    client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    QSignalSpy connected(&client, &net::ClientConnection::socketConnected);
    QSignalSpy events(&client, &net::ClientConnection::eventReceived);
    client.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 15000);

    bool ok = false;
    request(client, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("replier")},
                        {QStringLiteral("display_name"), QStringLiteral("Replier")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);

    const QJsonObject chatsReply = request(client, QStringLiteral("chat.list"), QJsonObject(), &ok);
    QVERIFY(ok);
    const double chatId = chatsReply.value(QStringLiteral("data")).toObject()
                             .value(QStringLiteral("chats")).toArray()
                             .first().toObject().value(QStringLiteral("id")).toDouble();

    const QJsonObject sent = request(
        client, QStringLiteral("msg.send"),
        QJsonObject{{QStringLiteral("chat_id"), chatId},
                    {QStringLiteral("client_msg_id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                    {QStringLiteral("kind"), QStringLiteral("text")},
                    {QStringLiteral("body"), QStringLiteral("первое")}},
        &ok);
    QVERIFY(ok);
    const double firstId = sent.value(QStringLiteral("data")).toObject()
                               .value(QStringLiteral("message_id")).toDouble();
    QVERIFY(firstId > 0);

    const QJsonObject reply = request(
        client, QStringLiteral("msg.send"),
        QJsonObject{{QStringLiteral("chat_id"), chatId},
                    {QStringLiteral("client_msg_id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                    {QStringLiteral("kind"), QStringLiteral("text")},
                    {QStringLiteral("body"), QStringLiteral("ответ")},
                    {QStringLiteral("reply_to"), firstId}},
        &ok);
    QVERIFY(ok);

    const QJsonObject opened = request(client, QStringLiteral("chat.open"),
                                       QJsonObject{{QStringLiteral("chat_id"), chatId},
                                                   {QStringLiteral("limit"), 10}},
                                       &ok);
    QVERIFY(ok);
    bool found = false;
    for (const QJsonValue &value : opened.value(QStringLiteral("data")).toObject()
                                        .value(QStringLiteral("messages")).toArray()) {
        const QJsonObject message = value.toObject();
        if (message.value(QStringLiteral("body")).toString() != QStringLiteral("ответ"))
            continue;
        QCOMPARE(message.value(QStringLiteral("reply_to_id")).toDouble(), firstId);
        found = true;
    }
    QVERIFY(found);

    // Ответ на несуществующее сообщение отклоняется.
    request(client, QStringLiteral("msg.send"),
            QJsonObject{{QStringLiteral("chat_id"), chatId},
                        {QStringLiteral("client_msg_id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                        {QStringLiteral("kind"), QStringLiteral("text")},
                        {QStringLiteral("body"), QStringLiteral("плохой ответ")},
                        {QStringLiteral("reply_to"), 999999}},
            &ok);
    QVERIFY(!ok);
}

void TestMessageOps::reactionsReachBothSides()
{
    net::ClientConnection first;
    net::ClientConnection second;
    first.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    second.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    QSignalSpy firstConnected(&first, &net::ClientConnection::socketConnected);
    QSignalSpy secondConnected(&second, &net::ClientConnection::socketConnected);
    QSignalSpy firstEvents(&first, &net::ClientConnection::eventReceived);

    first.connectToServer();
    second.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(firstConnected.count(), 1, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(secondConnected.count(), 1, 15000);

    bool ok = false;
    request(first, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("reactor_a")},
                        {QStringLiteral("display_name"), QStringLiteral("Reactor A")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);
    request(second, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("reactor_b")},
                        {QStringLiteral("display_name"), QStringLiteral("Reactor B")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);

    const QJsonObject chatsReply = request(first, QStringLiteral("chat.list"), QJsonObject(), &ok);
    QVERIFY(ok);
    const double chatId = chatsReply.value(QStringLiteral("data")).toObject()
                             .value(QStringLiteral("chats")).toArray()
                             .first().toObject().value(QStringLiteral("id")).toDouble();

    const QJsonObject sent = request(
        first, QStringLiteral("msg.send"),
        QJsonObject{{QStringLiteral("chat_id"), chatId},
                    {QStringLiteral("client_msg_id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                    {QStringLiteral("kind"), QStringLiteral("text")},
                    {QStringLiteral("body"), QStringLiteral("реакция")}},
        &ok);
    QVERIFY(ok);
    const double messageId = sent.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("message_id")).toDouble();
    QVERIFY(messageId > 0);

    request(second, QStringLiteral("msg.react"),
            QJsonObject{{QStringLiteral("chat_id"), chatId},
                        {QStringLiteral("message_id"), messageId},
                        {QStringLiteral("emoji"), QStringLiteral("👍")},
                        {QStringLiteral("on"), true}},
            &ok);
    QVERIFY(ok);

    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(firstEvents, QStringLiteral("ev.reaction")), 10000);
    const QJsonObject reaction = eventData(firstEvents, QStringLiteral("ev.reaction"));
    QCOMPARE(reaction.value(QStringLiteral("message_id")).toDouble(), messageId);
    QCOMPARE(reaction.value(QStringLiteral("emoji")).toString(), QStringLiteral("👍"));
    QVERIFY(reaction.value(QStringLiteral("on")).toBool());

    const QJsonObject opened = request(first, QStringLiteral("chat.open"),
                                       QJsonObject{{QStringLiteral("chat_id"), chatId},
                                                   {QStringLiteral("limit"), 10}},
                                       &ok);
    QVERIFY(ok);
    bool found = false;
    for (const QJsonValue &value : opened.value(QStringLiteral("data")).toObject()
                                        .value(QStringLiteral("messages")).toArray()) {
        const QJsonObject message = value.toObject();
        if (static_cast<qint64>(message.value(QStringLiteral("id")).toDouble()) != messageId)
            continue;
        const QJsonArray reactions = message.value(QStringLiteral("reactions")).toArray();
        QCOMPARE(reactions.size(), 1);
        QCOMPARE(reactions.first().toObject().value(QStringLiteral("count")).toInt(), 1);
        found = true;
    }
    QVERIFY(found);

    request(second, QStringLiteral("msg.react"),
            QJsonObject{{QStringLiteral("chat_id"), chatId},
                        {QStringLiteral("message_id"), messageId},
                        {QStringLiteral("emoji"), QStringLiteral("👍")},
                        {QStringLiteral("on"), false}},
            &ok);
    QVERIFY(ok);
}

void TestMessageOps::forwardCopiesTextToAnotherChat()
{
    net::ClientConnection first;
    net::ClientConnection second;
    first.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    second.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    QSignalSpy c1(&first, &net::ClientConnection::socketConnected);
    QSignalSpy c2(&second, &net::ClientConnection::socketConnected);
    first.connectToServer();
    second.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(c1.count(), 1, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(c2.count(), 1, 15000);

    bool ok = false;
    request(first, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("forward_a")},
                        {QStringLiteral("display_name"), QStringLiteral("Forward A")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);
    request(second, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("forward_b")},
                        {QStringLiteral("display_name"), QStringLiteral("Forward B")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);

    const QJsonObject chatsReply = request(first, QStringLiteral("chat.list"), QJsonObject(), &ok);
    QVERIFY(ok);
    const double generalId = chatsReply.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("chats")).toArray()
                                 .first().toObject().value(QStringLiteral("id")).toDouble();

    const QJsonObject sent = request(
        first, QStringLiteral("msg.send"),
        QJsonObject{{QStringLiteral("chat_id"), generalId},
                    {QStringLiteral("client_msg_id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                    {QStringLiteral("kind"), QStringLiteral("text")},
                    {QStringLiteral("body"), QStringLiteral("пересылаемый текст")}},
        &ok);
    QVERIFY(ok);
    const double messageId = sent.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("message_id")).toDouble();

    const QJsonObject direct = request(first, QStringLiteral("chat.direct"),
                                       QJsonObject{{QStringLiteral("username"),
                                                    QStringLiteral("forward_b")}},
                                       &ok);
    QVERIFY(ok);
    const double directId = direct.value(QStringLiteral("data")).toObject()
                                .value(QStringLiteral("chat")).toObject()
                                .value(QStringLiteral("id")).toDouble();

    request(first, QStringLiteral("msg.forward"),
            QJsonObject{{QStringLiteral("message_id"), messageId},
                        {QStringLiteral("chat_id"), directId}},
            &ok);
    QVERIFY(ok);

    const QJsonObject opened = request(second, QStringLiteral("chat.open"),
                                       QJsonObject{{QStringLiteral("chat_id"), directId},
                                                   {QStringLiteral("limit"), 10}},
                                       &ok);
    QVERIFY(ok);
    bool found = false;
    for (const QJsonValue &value : opened.value(QStringLiteral("data")).toObject()
                                        .value(QStringLiteral("messages")).toArray()) {
        if (value.toObject().value(QStringLiteral("body")).toString()
            == QStringLiteral("пересылаемый текст")) {
            found = true;
        }
    }
    QVERIFY(found);
}

void TestMessageOps::pinnedMessageAppearsInChat()
{
    net::ClientConnection first;
    net::ClientConnection second;
    first.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    second.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    QSignalSpy c1(&first, &net::ClientConnection::socketConnected);
    QSignalSpy c2(&second, &net::ClientConnection::socketConnected);
    QSignalSpy events(&second, &net::ClientConnection::eventReceived);
    first.connectToServer();
    second.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(c1.count(), 1, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(c2.count(), 1, 15000);

    bool ok = false;
    request(first, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("pin_owner")},
                        {QStringLiteral("display_name"), QStringLiteral("Pin Owner")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);
    request(second, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("pin_reader")},
                        {QStringLiteral("display_name"), QStringLiteral("Pin Reader")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);

    const QJsonObject chatsReply = request(first, QStringLiteral("chat.list"), QJsonObject(), &ok);
    QVERIFY(ok);
    const double chatId = chatsReply.value(QStringLiteral("data")).toObject()
                             .value(QStringLiteral("chats")).toArray()
                             .first().toObject().value(QStringLiteral("id")).toDouble();

    const QJsonObject sent = request(
        first, QStringLiteral("msg.send"),
        QJsonObject{{QStringLiteral("chat_id"), chatId},
                    {QStringLiteral("client_msg_id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                    {QStringLiteral("kind"), QStringLiteral("text")},
                    {QStringLiteral("body"), QStringLiteral("важное сообщение")}},
        &ok);
    QVERIFY(ok);
    const double messageId = sent.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("message_id")).toDouble();

    const QJsonObject pinReply = request(first, QStringLiteral("msg.pin"),
            QJsonObject{{QStringLiteral("chat_id"), chatId},
                        {QStringLiteral("message_id"), messageId},
                        {QStringLiteral("on"), true}},
            &ok);
    QVERIFY2(ok, qPrintable(pinReply.value(QStringLiteral("error")).toObject()
                              .value(QStringLiteral("message")).toString()));

    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(events, QStringLiteral("ev.pin")), 10000);

    const QJsonObject opened = request(second, QStringLiteral("chat.open"),
                                       QJsonObject{{QStringLiteral("chat_id"), chatId},
                                                   {QStringLiteral("limit"), 10}},
                                       &ok);
    QVERIFY(ok);
    const QJsonArray pinned = opened.value(QStringLiteral("data")).toObject()
                                  .value(QStringLiteral("pinned")).toArray();
    QCOMPARE(pinned.size(), 1);
    QCOMPARE(pinned.first().toObject().value(QStringLiteral("body")).toString(),
             QStringLiteral("важное сообщение"));

    request(first, QStringLiteral("msg.pin"),
            QJsonObject{{QStringLiteral("chat_id"), chatId},
                        {QStringLiteral("message_id"), messageId},
                        {QStringLiteral("on"), false}},
            &ok);
    QVERIFY(ok);
}

void TestMessageOps::chatFlagsArePersisted()
{
    net::ClientConnection client;
    client.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    QSignalSpy connected(&client, &net::ClientConnection::socketConnected);
    client.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 15000);

    bool ok = false;
    request(client, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("flags_user")},
                        {QStringLiteral("display_name"), QStringLiteral("Flags User")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);

    const QJsonObject chatsReply = request(client, QStringLiteral("chat.list"), QJsonObject(), &ok);
    QVERIFY(ok);
    const double chatId = chatsReply.value(QStringLiteral("data")).toObject()
                             .value(QStringLiteral("chats")).toArray()
                             .first().toObject().value(QStringLiteral("id")).toDouble();

    request(client, QStringLiteral("chat.update"),
            QJsonObject{{QStringLiteral("chat_id"), chatId},
                        {QStringLiteral("pinned"), true},
                        {QStringLiteral("muted"), true}},
            &ok);
    QVERIFY(ok);

    const QJsonObject after = request(client, QStringLiteral("chat.list"), QJsonObject(), &ok);
    QVERIFY(ok);
    const QJsonObject summary = after.value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("chats")).toArray()
                                    .first().toObject();
    QVERIFY(summary.value(QStringLiteral("pinned")).toBool());
    QVERIFY(summary.value(QStringLiteral("muted")).toBool());

    request(client, QStringLiteral("chat.update"),
            QJsonObject{{QStringLiteral("chat_id"), chatId},
                        {QStringLiteral("archived"), true}},
            &ok);
    QVERIFY(ok);

    const QJsonObject archived = request(client, QStringLiteral("chat.list"), QJsonObject(), &ok);
    QVERIFY(ok);
    QVERIFY(archived.value(QStringLiteral("data")).toObject()
                .value(QStringLiteral("chats")).toArray()
                .first().toObject().value(QStringLiteral("archived")).toBool());
}

void TestMessageOps::voicePubkeysAreRelayed()
{
    net::ClientConnection first;
    net::ClientConnection second;
    first.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    second.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    QSignalSpy c1(&first, &net::ClientConnection::socketConnected);
    QSignalSpy c2(&second, &net::ClientConnection::socketConnected);
    QSignalSpy events(&second, &net::ClientConnection::eventReceived);
    first.connectToServer();
    second.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(c1.count(), 1, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(c2.count(), 1, 15000);

    bool ok = false;
    request(first, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("voice_a")},
                        {QStringLiteral("display_name"), QStringLiteral("Voice A")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);
    request(second, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("voice_b")},
                        {QStringLiteral("display_name"), QStringLiteral("Voice B")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);

    const QJsonObject direct = request(first, QStringLiteral("chat.direct"),
                                       QJsonObject{{QStringLiteral("username"),
                                                    QStringLiteral("voice_b")}},
                                       &ok);
    QVERIFY(ok);
    const double chatId = direct.value(QStringLiteral("data")).toObject()
                              .value(QStringLiteral("chat")).toObject()
                              .value(QStringLiteral("id")).toDouble();

    request(first, QStringLiteral("voice.join"),
            QJsonObject{{QStringLiteral("chat_id"), chatId}}, &ok);
    QVERIFY(ok);
    request(second, QStringLiteral("voice.join"),
            QJsonObject{{QStringLiteral("chat_id"), chatId}}, &ok);
    QVERIFY(ok);

    QByteArray pubkey(32, 'p');
    const QString encoded = QString::fromLatin1(pubkey.toBase64());
    request(first, QStringLiteral("voice.pubkey"),
            QJsonObject{{QStringLiteral("chat_id"), chatId},
                        {QStringLiteral("pubkey"), encoded}},
            &ok);
    QVERIFY(ok);

    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(events, QStringLiteral("ev.voice_pubkey")), 10000);
    const QJsonObject event = eventData(events, QStringLiteral("ev.voice_pubkey"));
    QCOMPARE(event.value(QStringLiteral("pubkey")).toString(), encoded);
}

void TestMessageOps::foreignMessageCannotBeChanged()
{
    net::ClientConnection author;
    net::ClientConnection stranger;
    author.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    stranger.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    QSignalSpy authorConnected(&author, &net::ClientConnection::socketConnected);
    QSignalSpy strangerConnected(&stranger, &net::ClientConnection::socketConnected);
    author.connectToServer();
    stranger.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(authorConnected.count(), 1, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(strangerConnected.count(), 1, 15000);

    bool ok = false;
    request(author, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("owner")},
                        {QStringLiteral("display_name"), QStringLiteral("Owner")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);

    request(stranger, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("intruder")},
                        {QStringLiteral("display_name"), QStringLiteral("Intruder")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);

    const QJsonObject chatsReply = request(author, QStringLiteral("chat.list"), QJsonObject(), &ok);
    QVERIFY(ok);
    const double chatId = chatsReply.value(QStringLiteral("data")).toObject()
                              .value(QStringLiteral("chats")).toArray()
                              .first().toObject().value(QStringLiteral("id")).toDouble();

    const QJsonObject sendReply = request(
        author, QStringLiteral("msg.send"),
        QJsonObject{{QStringLiteral("chat_id"), chatId},
                    {QStringLiteral("client_msg_id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                    {QStringLiteral("kind"), QStringLiteral("text")},
                    {QStringLiteral("body"), QStringLiteral("чужое сообщение")}},
        &ok);
    QVERIFY(ok);
    const double messageId = sendReply.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("message_id")).toDouble();

    const QJsonObject editReply = request(
        stranger, QStringLiteral("msg.edit"),
        QJsonObject{{QStringLiteral("chat_id"), chatId},
                    {QStringLiteral("message_id"), messageId},
                    {QStringLiteral("body"), QStringLiteral("подмена")}});
    QVERIFY(!editReply.value(QStringLiteral("ok")).toBool(false));

    const QJsonObject deleteReply = request(
        stranger, QStringLiteral("msg.delete"),
        QJsonObject{{QStringLiteral("chat_id"), chatId},
                    {QStringLiteral("message_id"), messageId}});
    QVERIFY(!deleteReply.value(QStringLiteral("ok")).toBool(false));

    author.disconnectFromServer();
    stranger.disconnectFromServer();
}

QTEST_GUILESS_MAIN(TestMessageOps)
#include "tst_message_ops.moc"
