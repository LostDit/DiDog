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

QJsonObject eventData(const QSignalSpy &spy, const QString &name)
{
    for (const QList<QVariant> &arguments : spy) {
        if (arguments.value(0).toString() == name)
            return arguments.value(1).value<QJsonObject>();
    }
    return QJsonObject();
}

} // namespace

// Сигнализация вызова: приглашение, принятие, отклонение и отмена.
class TestVoiceCall : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void inviteAcceptDeclineCancel();

private:
    QJsonObject request(net::ClientConnection &connection, const QString &type,
                        const QJsonObject &data, bool *ok = nullptr);
    double registerUser(net::ClientConnection &connection, const QString &username);

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<server::ServerApp> m_server;
};

void TestVoiceCall::init()
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

void TestVoiceCall::cleanup()
{
    m_server.reset();
    m_dir.reset();
}

QJsonObject TestVoiceCall::request(net::ClientConnection &connection, const QString &type,
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

double TestVoiceCall::registerUser(net::ClientConnection &connection, const QString &username)
{
    bool ok = false;
    const QJsonObject reply = request(
        connection, QStringLiteral("auth.register"),
        QJsonObject{{QStringLiteral("username"), username},
                    {QStringLiteral("display_name"), username},
                    {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
        &ok);
    if (!ok)
        return 0.0;
    return reply.value(QStringLiteral("data")).toObject()
        .value(QStringLiteral("user")).toObject()
        .value(QStringLiteral("id")).toDouble();
}

void TestVoiceCall::inviteAcceptDeclineCancel()
{
    net::ClientConnection caller;
    net::ClientConnection callee;
    caller.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    callee.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    QSignalSpy callerConnected(&caller, &net::ClientConnection::socketConnected);
    QSignalSpy calleeConnected(&callee, &net::ClientConnection::socketConnected);
    QSignalSpy callerEvents(&caller, &net::ClientConnection::eventReceived);
    QSignalSpy calleeEvents(&callee, &net::ClientConnection::eventReceived);

    caller.connectToServer();
    callee.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(callerConnected.count(), 1, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(calleeConnected.count(), 1, 15000);

    const double callerId = registerUser(caller, QStringLiteral("caller_user"));
    const double calleeId = registerUser(callee, QStringLiteral("callee_user"));

    bool ok = false;
    const QJsonObject chatsReply = request(caller, QStringLiteral("chat.list"), QJsonObject(), &ok);
    QVERIFY(ok);
    const double chatId = chatsReply.value(QStringLiteral("data")).toObject()
                              .value(QStringLiteral("chats")).toArray()
                              .first().toObject().value(QStringLiteral("id")).toDouble();

    // Приглашение доходит до второго участника.
    request(caller, QStringLiteral("voice.invite"),
            QJsonObject{{QStringLiteral("chat_id"), chatId}}, &ok);
    QVERIFY(ok);

    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(calleeEvents, QStringLiteral("ev.voice.invite")), 5000);
    const QJsonObject invite =
        eventData(calleeEvents, QStringLiteral("ev.voice.invite"));
    QCOMPARE(invite.value(QStringLiteral("caller_id")).toDouble(), callerId);

    // Принятие: вызывающий получает ev.voice.accepted.
    request(callee, QStringLiteral("voice.accept"),
            QJsonObject{{QStringLiteral("chat_id"), chatId},
                        {QStringLiteral("caller_id"), callerId}},
            &ok);
    QVERIFY(ok);

    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(callerEvents, QStringLiteral("ev.voice.accepted")), 5000);
    QCOMPARE(eventData(callerEvents, QStringLiteral("ev.voice.accepted"))
                 .value(QStringLiteral("user_id")).toDouble(),
             calleeId);

    // Отклонение.
    request(caller, QStringLiteral("voice.invite"),
            QJsonObject{{QStringLiteral("chat_id"), chatId}}, &ok);
    QVERIFY(ok);
    request(callee, QStringLiteral("voice.decline"),
            QJsonObject{{QStringLiteral("chat_id"), chatId},
                        {QStringLiteral("caller_id"), callerId}},
            &ok);
    QVERIFY(ok);
    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(callerEvents, QStringLiteral("ev.voice.declined")), 5000);

    // Отмена вызова.
    request(caller, QStringLiteral("voice.invite"),
            QJsonObject{{QStringLiteral("chat_id"), chatId}}, &ok);
    QVERIFY(ok);
    request(caller, QStringLiteral("voice.cancel"),
            QJsonObject{{QStringLiteral("chat_id"), chatId}}, &ok);
    QVERIFY(ok);
    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(calleeEvents, QStringLiteral("ev.voice.cancelled")), 5000);

    caller.disconnectFromServer();
    callee.disconnectFromServer();
}

QTEST_GUILESS_MAIN(TestVoiceCall)
#include "tst_voice_call.moc"
