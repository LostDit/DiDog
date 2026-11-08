#include "core/Protocol.h"
#include "net/ClientConnection.h"
#include "server/ServerApp.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// Обновления: манифест и скачивание файла с произвольного смещения (докачка).
class TestUpdate : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void manifestAndResumedDownload();

private:
    QJsonObject request(net::ClientConnection &connection, const QString &type,
                        const QJsonObject &data, bool *ok = nullptr);

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<server::ServerApp> m_server;
    QByteArray m_content;
};

void TestUpdate::init()
{
    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());

    m_content = QByteArray(5000, 'x');
    for (int i = 0; i < m_content.size(); ++i)
        m_content[i] = static_cast<char>('a' + (i * 7) % 26);

    const QString updatesDir = m_dir->filePath(QStringLiteral("updates"));
    QVERIFY(QDir().mkpath(updatesDir));

    QFile payload(updatesDir + QStringLiteral("/client.bin"));
    QVERIFY(payload.open(QIODevice::WriteOnly));
    payload.write(m_content);
    payload.close();

    QJsonObject manifest;
    manifest.insert(QStringLiteral("version"), QStringLiteral("9.9.9"));
    QJsonObject item;
    item.insert(QStringLiteral("path"), QStringLiteral("client.bin"));
    item.insert(QStringLiteral("size"), m_content.size());
    item.insert(QStringLiteral("sha256"),
                QString::fromLatin1(QCryptographicHash::hash(m_content,
                                                             QCryptographicHash::Sha256)
                                        .toHex()));
    manifest.insert(QStringLiteral("files"), QJsonArray{item});

    QFile manifestFile(updatesDir + QStringLiteral("/manifest.json"));
    QVERIFY(manifestFile.open(QIODevice::WriteOnly));
    manifestFile.write(QJsonDocument(manifest).toJson());
    manifestFile.close();

    m_server.reset(new server::ServerApp);
    m_server->setUpdatesRoot(updatesDir);

    QString error;
    QVERIFY2(m_server->initialize(m_dir->filePath(QStringLiteral("server.db")),
                                  m_dir->filePath(QStringLiteral("attachments")), &error),
             qPrintable(error));
    QVERIFY2(m_server->start(0), qPrintable(m_server->errorString()));
}

void TestUpdate::cleanup()
{
    m_server.reset();
    m_dir.reset();
}

QJsonObject TestUpdate::request(net::ClientConnection &connection, const QString &type,
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

void TestUpdate::manifestAndResumedDownload()
{
    net::ClientConnection connection;
    connection.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    QSignalSpy connected(&connection, &net::ClientConnection::socketConnected);
    connection.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 15000);

    bool ok = false;
    request(connection, QStringLiteral("auth.register"),
            QJsonObject{{QStringLiteral("username"), QStringLiteral("updater")},
                        {QStringLiteral("display_name"), QStringLiteral("Updater")},
                        {QStringLiteral("password"), QStringLiteral("probe-password-123")}},
            &ok);
    QVERIFY(ok);

    const QJsonObject manifestReply = request(connection, QStringLiteral("update.manifest"),
                                              QJsonObject(), &ok);
    QVERIFY(ok);
    const QJsonObject manifest = manifestReply.value(QStringLiteral("data")).toObject();
    QCOMPARE(manifest.value(QStringLiteral("version")).toString(), QStringLiteral("9.9.9"));
    const QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.first().toObject().value(QStringLiteral("path")).toString(),
             QStringLiteral("client.bin"));

    // Докачка с середины: offset = 1234.
    const qint64 offset = 1234;

    QByteArray received;
    QSignalSpy finished(&connection, &net::ClientConnection::transferFinished);
    quint32 transferId = 0;

    QObject::connect(&connection, &net::ClientConnection::chunkReceived,
                     [&received](quint32, quint32, const QByteArray &chunk) {
                         received.append(chunk);
                     });

    const QJsonObject downloadReply = request(
        connection, QStringLiteral("update.download"),
        QJsonObject{{QStringLiteral("path"), QStringLiteral("client.bin")},
                    {QStringLiteral("offset"), static_cast<double>(offset)}},
        &ok);
    QVERIFY(ok);

    const QJsonObject data = downloadReply.value(QStringLiteral("data")).toObject();
    transferId = static_cast<quint32>(data.value(QStringLiteral("transfer_id")).toDouble());
    QCOMPARE(static_cast<qint64>(data.value(QStringLiteral("size")).toDouble()),
             static_cast<qint64>(m_content.size()));

    QTRY_VERIFY_WITH_TIMEOUT(received.size() >= m_content.size() - offset, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 10000);

    QCOMPARE(received, m_content.mid(offset));

    // Выход за пределы каталога обновлений запрещён.
    const QJsonObject traversal = request(
        connection, QStringLiteral("update.download"),
        QJsonObject{{QStringLiteral("path"), QStringLiteral("../server.db")},
                    {QStringLiteral("offset"), 0}});
    QVERIFY(!traversal.value(QStringLiteral("ok")).toBool(false));

    connection.disconnectFromServer();
}

QTEST_GUILESS_MAIN(TestUpdate)
#include "tst_update.moc"
