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

// Подписка: статус, промокоды и доступ к премиальным темам.
class TestBilling : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void promoActivatesSubscription();

private:
    QJsonObject request(net::ClientConnection &connection, const QString &type,
                        const QJsonObject &data, bool *ok = nullptr);

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<server::ServerApp> m_server;
};

void TestBilling::init()
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

void TestBilling::cleanup()
{
    m_server.reset();
    m_dir.reset();
}

QJsonObject TestBilling::request(net::ClientConnection &connection, const QString &type,
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

void TestBilling::promoActivatesSubscription()
{
    QVERIFY(m_server->createPromo(QStringLiteral("WELCOME30"), QStringLiteral("plus"), 30,
                                  1, nullptr));

    net::ClientConnection connection;
    connection.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    QSignalSpy connected(&connection, &net::ClientConnection::socketConnected);
    connection.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 15000);

    bool ok = false;
    request(connection, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("buyer")},
                        {QStringLiteral("display_name"), QStringLiteral("Buyer")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);

    // Бесплатный план: премиальная тема недоступна.
    const QJsonObject freeStatus = request(connection, QStringLiteral("billing.status"),
                                           QJsonObject(), &ok);
    QVERIFY(ok);
    QCOMPARE(freeStatus.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("plan")).toString(),
             QStringLiteral("free"));

    const QJsonObject denied = request(connection, QStringLiteral("theme.get"),
                                       QJsonObject{{QStringLiteral("id"), QStringLiteral("midnight")}});
    QVERIFY(!denied.value(QStringLiteral("ok")).toBool(false));

    // Список тем содержит премиальные пакеты.
    const QJsonObject themesReply = request(connection, QStringLiteral("theme.list"),
                                            QJsonObject(), &ok);
    QVERIFY(ok);
    const QJsonArray themes = themesReply.value(QStringLiteral("data")).toObject()
                                  .value(QStringLiteral("themes")).toArray();
    QVERIFY(themes.size() >= 2);
    bool premiumFound = false;
    for (const QJsonValue &value : themes) {
        if (value.toObject().value(QStringLiteral("premium")).toBool(false))
            premiumFound = true;
    }
    QVERIFY(premiumFound);

    // Неверный код.
    const QJsonObject badCode = request(connection, QStringLiteral("billing.redeem"),
                                        QJsonObject{{QStringLiteral("code"), QStringLiteral("NOPE")}});
    QVERIFY(!badCode.value(QStringLiteral("ok")).toBool(false));

    // Активация.
    const QJsonObject redeemed = request(connection, QStringLiteral("billing.redeem"),
                                         QJsonObject{{QStringLiteral("code"), QStringLiteral("WELCOME30")}},
                                         &ok);
    QVERIFY(ok);
    QCOMPARE(redeemed.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("plan")).toString(),
             QStringLiteral("plus"));
    QVERIFY(redeemed.value(QStringLiteral("data")).toObject()
                .value(QStringLiteral("expires_at")).toDouble() > 0);

    // Повторная активация того же кода невозможна (лимит 1).
    const QJsonObject again = request(connection, QStringLiteral("billing.redeem"),
                                      QJsonObject{{QStringLiteral("code"), QStringLiteral("WELCOME30")}});
    QVERIFY(!again.value(QStringLiteral("ok")).toBool(false));

    // Статус обновился, тема доступна.
    const QJsonObject paidStatus = request(connection, QStringLiteral("billing.status"),
                                           QJsonObject(), &ok);
    QVERIFY(ok);
    const QJsonObject statusData = paidStatus.value(QStringLiteral("data")).toObject();
    QCOMPARE(statusData.value(QStringLiteral("plan")).toString(), QStringLiteral("plus"));
    QVERIFY(statusData.value(QStringLiteral("entitlements")).toArray().contains(
        QStringLiteral("themes.premium")));

    const QJsonObject theme = request(connection, QStringLiteral("theme.get"),
                                      QJsonObject{{QStringLiteral("id"), QStringLiteral("midnight")}},
                                      &ok);
    QVERIFY(ok);
    const QJsonObject tokens = theme.value(QStringLiteral("data")).toObject()
                                   .value(QStringLiteral("theme")).toObject()
                                   .value(QStringLiteral("tokens")).toObject();
    QVERIFY(tokens.contains(QStringLiteral("bg")));
    QVERIFY(tokens.contains(QStringLiteral("accent")));

    connection.disconnectFromServer();
}

QTEST_GUILESS_MAIN(TestBilling)
#include "tst_billing.moc"
