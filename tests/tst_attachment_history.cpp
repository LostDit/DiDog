#include "app/AttachmentService.h"
#include "app/ChatService.h"
#include "app/SessionService.h"
#include "net/ClientConnection.h"
#include "server/ServerApp.h"

#include <QFile>
#include <QFileInfo>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// Регрессия: вложения из истории (chat.open) должны докачиваться после
// перезапуска клиента, а не висеть в состоянии «Загрузка…».
class TestAttachmentHistory : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void historyAttachmentIsDownloaded();

private:
    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<server::ServerApp> m_server;
};

void TestAttachmentHistory::init()
{
    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());

    const QString certificate = QStringLiteral(TEST_SOURCE_DIR "/resources/tls/server.crt");
    const QString key = QStringLiteral(TEST_SOURCE_DIR "/resources/tls/server.key");

    m_server.reset(new server::ServerApp);

    QString tlsError;
    QVERIFY2(m_server->enableTls(certificate, key, &tlsError), qPrintable(tlsError));

    QString error;
    QVERIFY2(m_server->initialize(m_dir->filePath(QStringLiteral("server.db")),
                                  m_dir->filePath(QStringLiteral("attachments")), &error),
             qPrintable(error));
    QVERIFY2(m_server->start(0), qPrintable(m_server->errorString()));
}

void TestAttachmentHistory::cleanup()
{
    m_server.reset();
    m_dir.reset();
}

void TestAttachmentHistory::historyAttachmentIsDownloaded()
{
    net::ClientConnection connection;
    connection.setTlsEnabled(true);
    connection.setEndpoint(QStringLiteral("127.0.0.1"), m_server->port());

    QSignalSpy connected(&connection, &net::ClientConnection::socketConnected);
    connection.connectToServer();
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 15000);

    app::SessionService session(&connection, nullptr);
    QSignalSpy authenticated(&session, &app::SessionService::authenticated);
    session.registerUser(QStringLiteral("probe_history"), QStringLiteral("Probe"),
                         QStringLiteral("probe-password-123"));
    QTRY_COMPARE_WITH_TIMEOUT(authenticated.count(), 1, 15000);

    const data::User user =
        authenticated.first().at(0).value<data::User>();
    QVERIFY(user.id > 0);

    app::AttachmentService attachments(&connection, m_dir->filePath(QStringLiteral("cache-one")));
    app::ChatService chats(&connection, nullptr, &attachments);
    chats.setCurrentUser(user);
    chats.loadChats();

    QTRY_VERIFY_WITH_TIMEOUT(!chats.chats().isEmpty(), 15000);
    const qint64 chatId = chats.chats().first().chat.id;

    QByteArray payload;
    payload.reserve(20000);
    for (int i = 0; i < 20000; ++i)
        payload.append(static_cast<char>((i * 13 + 7) & 0xFF));

    const QString sourcePath = m_dir->filePath(QStringLiteral("photo.png"));
    {
        QFile file(sourcePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(payload);
    }

    chats.sendFile(chatId, sourcePath);

    // Ждём, пока сообщение получит id и вложение сохранится на сервере.
    const auto hasSentAttachment = [&chats, chatId]() {
        for (const data::Message &message : chats.messages(chatId)) {
            if (message.hasAttachment() && message.attachment.id > 0
                && message.state == data::DeliveryState::Sent) {
                return true;
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(hasSentAttachment(), 20000);

    // «Перезапуск»: новый кэш и новый ChatService читают историю с сервера.
    app::AttachmentService freshAttachments(&connection,
                                            m_dir->filePath(QStringLiteral("cache-two")));
    app::ChatService freshChats(&connection, nullptr, &freshAttachments);
    freshChats.setCurrentUser(user);
    freshChats.loadChats();
    QTRY_VERIFY_WITH_TIMEOUT(!freshChats.chats().isEmpty(), 15000);

    freshChats.openChat(chatId);

    const auto hasDownloadedAttachment = [&freshChats, chatId]() {
        for (const data::Message &message : freshChats.messages(chatId)) {
            if (message.hasAttachment() && !message.localPath.isEmpty()
                && QFileInfo::exists(message.localPath)) {
                return true;
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(hasDownloadedAttachment(), 20000);

    QString downloaded;
    for (const data::Message &message : freshChats.messages(chatId)) {
        if (message.hasAttachment() && !message.localPath.isEmpty())
            downloaded = message.localPath;
    }

    QVERIFY(!downloaded.isEmpty());
    QFile file(downloaded);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), payload);

    connection.disconnectFromServer();
}

QTEST_GUILESS_MAIN(TestAttachmentHistory)
#include "tst_attachment_history.moc"
