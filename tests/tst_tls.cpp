#include "core/Protocol.h"
#include "net/ClientConnection.h"
#include "server/ServerApp.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestTls : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void handshakeSucceedsAndPinsCertificate();
    void pinMismatchIsRejected();

private:
    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<server::ServerApp> m_server;
};

void TestTls::init()
{
    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());

    const QString certificate = QStringLiteral(TEST_SOURCE_DIR "/resources/tls/server.crt");
    const QString key = QStringLiteral(TEST_SOURCE_DIR "/resources/tls/server.key");
    QVERIFY2(QFileInfo::exists(certificate), qPrintable(certificate));
    QVERIFY2(QFileInfo::exists(key), qPrintable(key));

    m_server.reset(new server::ServerApp);

    QString tlsError;
    QVERIFY2(m_server->enableTls(certificate, key, &tlsError), qPrintable(tlsError));

    QString error;
    QVERIFY2(m_server->initialize(m_dir->filePath(QStringLiteral("server.db")),
                                  m_dir->filePath(QStringLiteral("attachments")), &error),
             qPrintable(error));
    QVERIFY2(m_server->start(0), qPrintable(m_server->errorString()));
}

void TestTls::cleanup()
{
    m_server.reset();
    m_dir.reset();
}

void TestTls::handshakeSucceedsAndPinsCertificate()
{
    net::ClientConnection connection;
    connection.setTlsEnabled(true);
    connection.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    QSignalSpy connected(&connection, &net::ClientConnection::socketConnected);
    QSignalSpy pinned(&connection, &net::ClientConnection::certificatePinned);
    QSignalSpy errors(&connection, &net::ClientConnection::errorOccurred);

    connection.connectToServer();

    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 15000);
    QCOMPARE(errors.count(), 0);
    QVERIFY(!connection.peerCertificatePin().isEmpty());
    QVERIFY(pinned.count() >= 1);

    bool replied = false;
    bool ok = false;

    QJsonObject hello;
    hello.insert(QStringLiteral("app"), QStringLiteral("Test/1.0"));
    hello.insert(QStringLiteral("proto"), 1);

    connection.request(QStringLiteral("hello"), hello, [&](const QJsonObject &reply) {
        replied = true;
        ok = reply.value(QStringLiteral("ok")).toBool(false);
    });

    QTRY_VERIFY_WITH_TIMEOUT(replied, 10000);
    QVERIFY(ok);

    connection.disconnectFromServer();
}

void TestTls::pinMismatchIsRejected()
{
    net::ClientConnection connection;
    connection.setTlsEnabled(true);
    connection.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());
    connection.setExpectedCertificatePin(QString(64, QLatin1Char('0')));

    QSignalSpy connected(&connection, &net::ClientConnection::socketConnected);
    QSignalSpy errors(&connection, &net::ClientConnection::errorOccurred);

    connection.connectToServer();

    QTRY_VERIFY_WITH_TIMEOUT(errors.count() >= 1, 15000);
    QCOMPARE(connected.count(), 0);
    QCOMPARE(connection.state(), net::ClientConnection::State::Disconnected);

    QVERIFY2(errors.first().at(0).toString().contains(QStringLiteral("сертификат"),
                                                      Qt::CaseInsensitive),
             qPrintable(errors.first().at(0).toString()));
}

QTEST_GUILESS_MAIN(TestTls)
#include "tst_tls.moc"
