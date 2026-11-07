#include "app/SessionService.h"
#include "app/SettingsStore.h"
#include "net/ClientConnection.h"
#include "server/ServerApp.h"

#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestSession : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registersAndStoresToken();
    void rejectsWrongPassword();
    void logoutClearsToken();
    void resumesSavedSession();

private:
    bool waitConnected(net::ClientConnection &connection);

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<server::ServerApp> m_server;
};

void TestSession::init()
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

void TestSession::cleanup()
{
    m_server.reset();
    m_dir.reset();
}

bool TestSession::waitConnected(net::ClientConnection &connection)
{
    QSignalSpy spy(&connection, &net::ClientConnection::socketConnected);
    connection.connectToServer();
    if (!spy.wait(5000))
        return false;
    return connection.isConnected();
}

void TestSession::registersAndStoresToken()
{
    net::ClientConnection connection;
    connection.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    app::SettingsStore settings(m_dir->filePath(QStringLiteral("settings.ini")));
    app::SessionService session(&connection, &settings);

    QSignalSpy authenticated(&session, &app::SessionService::authenticated);
    QSignalSpy failed(&session, &app::SessionService::authFailed);

    QVERIFY(waitConnected(connection));
    session.registerUser(QStringLiteral("alice"), QStringLiteral("Алиса"),
                         QStringLiteral("password123"));

    QTRY_COMPARE_WITH_TIMEOUT(authenticated.count(), 1, 10000);
    QCOMPARE(failed.count(), 0);
    QVERIFY(session.isAuthenticated());
    QCOMPARE(session.user().username, QStringLiteral("alice"));
    QCOMPARE(session.user().displayName, QStringLiteral("Алиса"));
    QVERIFY(!session.token().isEmpty());
    QCOMPARE(settings.authToken(), session.token());
}

void TestSession::rejectsWrongPassword()
{
    net::ClientConnection connection;
    connection.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    app::SettingsStore settings(m_dir->filePath(QStringLiteral("settings.ini")));
    app::SessionService session(&connection, &settings);

    QSignalSpy authenticated(&session, &app::SessionService::authenticated);
    QSignalSpy failed(&session, &app::SessionService::authFailed);

    QVERIFY(waitConnected(connection));
    session.registerUser(QStringLiteral("bob"), QStringLiteral("Боб"),
                         QStringLiteral("password123"));
    QTRY_COMPARE_WITH_TIMEOUT(authenticated.count(), 1, 10000);

    session.logout();
    QTest::qWait(100);

    session.login(QStringLiteral("bob"), QStringLiteral("wrong-password"), true);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 10000);
    QVERIFY(!session.isAuthenticated());
    QVERIFY(!failed.first().at(0).toString().isEmpty());
}

void TestSession::logoutClearsToken()
{
    net::ClientConnection connection;
    connection.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    app::SettingsStore settings(m_dir->filePath(QStringLiteral("settings.ini")));
    app::SessionService session(&connection, &settings);

    QSignalSpy authenticated(&session, &app::SessionService::authenticated);
    QSignalSpy loggedOut(&session, &app::SessionService::loggedOut);

    QVERIFY(waitConnected(connection));
    session.registerUser(QStringLiteral("carol"), QStringLiteral("Кэрол"),
                         QStringLiteral("password123"));
    QTRY_COMPARE_WITH_TIMEOUT(authenticated.count(), 1, 10000);
    QVERIFY(!settings.authToken().isEmpty());

    session.logout();
    QCOMPARE(loggedOut.count(), 1);
    QVERIFY(!session.isAuthenticated());
    QVERIFY(settings.authToken().isEmpty());
}

void TestSession::resumesSavedSession()
{
    QString token;
    {
        net::ClientConnection connection;
        connection.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

        app::SettingsStore settings(m_dir->filePath(QStringLiteral("settings.ini")));
        app::SessionService session(&connection, &settings);

        QSignalSpy authenticated(&session, &app::SessionService::authenticated);
        QVERIFY(waitConnected(connection));
        session.registerUser(QStringLiteral("dave"), QStringLiteral("Дэйв"),
                             QStringLiteral("password123"));
        QTRY_COMPARE_WITH_TIMEOUT(authenticated.count(), 1, 10000);
        token = settings.authToken();
        QVERIFY(!token.isEmpty());

        connection.disconnectFromServer();
    }

    net::ClientConnection second;
    second.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    app::SettingsStore settings(m_dir->filePath(QStringLiteral("settings.ini")));
    QCOMPARE(settings.authToken(), token);

    app::SessionService session(&second, &settings);
    QSignalSpy authenticated(&session, &app::SessionService::authenticated);

    QVERIFY(waitConnected(second));
    QTRY_COMPARE_WITH_TIMEOUT(authenticated.count(), 1, 10000);
    QCOMPARE(session.user().username, QStringLiteral("dave"));
}

QTEST_GUILESS_MAIN(TestSession)
#include "tst_session.moc"
