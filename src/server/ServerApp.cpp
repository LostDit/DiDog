#include "server/ServerApp.h"

#include "server/ClientSession.h"
#include "server/ServerCore.h"
#include "server/TlsServer.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>

namespace server {
namespace {

// IPv4-адреса машины в локальной сети — их вводят в клиенте на других компьютерах.
QStringList lanAddresses()
{
    QStringList result;

    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol)
            continue;
        if (address.isLoopback())
            continue;
        result.append(address.toString());
    }

    result.removeDuplicates();
    return result;
}

} // namespace

ServerApp::ServerApp(QObject *parent)
    : QObject(parent)
    , m_core(new ServerCore(this))
{
}

ServerApp::~ServerApp()
{
    stop();
}

bool ServerApp::enableTls(const QString &certificatePath, const QString &keyPath, QString *error)
{
    const QList<QSslCertificate> certificates =
        QSslCertificate::fromPath(certificatePath, QSsl::Pem);

    if (certificates.isEmpty()) {
        if (error)
            *error = QStringLiteral("Не удалось прочитать сертификат: %1").arg(certificatePath);
        return false;
    }

    QFile keyFile(keyPath);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Не удалось открыть ключ: %1").arg(keyPath);
        return false;
    }

    QSslKey key(&keyFile, QSsl::Rsa, QSsl::Pem);
    if (key.isNull()) {
        if (error)
            *error = QStringLiteral("Не удалось прочитать ключ: %1").arg(keyPath);
        return false;
    }

    m_tlsConfiguration = QSslConfiguration::defaultConfiguration();
    m_tlsConfiguration.setLocalCertificate(certificates.first());
    m_tlsConfiguration.setPrivateKey(key);
    m_tlsConfiguration.setPeerVerifyMode(QSslSocket::VerifyNone);
    m_tlsConfiguration.setProtocol(QSsl::TlsV1_2OrLater);

    qInfo().noquote() << QStringLiteral("Сертификат сервера: %1")
                             .arg(certificates.first().subjectInfo(QSslCertificate::CommonName)
                                      .join(QStringLiteral(", ")));
    qInfo().noquote() << QStringLiteral("Отпечаток SHA-256: %1")
                             .arg(QString::fromLatin1(
                                 certificates.first()
                                     .digest(QCryptographicHash::Sha256)
                                     .toHex()));

    m_tlsEnabled = true;
    return true;
}

bool ServerApp::initialize(const QString &databasePath, const QString &attachmentsRoot,
                           QString *error)
{
    return m_core->initialize(databasePath, attachmentsRoot, error);
}

bool ServerApp::createPromo(const QString &code, const QString &plan, int days, int maxUses,
                            QString *error)
{
    return m_core->createPromo(code, plan, days, maxUses, error);
}

void ServerApp::createServer()
{
    if (m_server)
        return;

    if (m_tlsEnabled) {
        auto *tlsServer = new TlsServer(m_tlsConfiguration, this);
        connect(tlsServer, &TlsServer::securedConnection, this, [this](QSslSocket *socket) {
            attachSession(socket);
        });
        m_server = tlsServer;
    } else {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &ServerApp::onNewConnection);
    }
}

bool ServerApp::start(quint16 port)
{
    createServer();

    if (m_server->isListening())
        return true;

    if (!m_server->listen(QHostAddress::Any, port)) {
        m_error = m_server->errorString();
        qCritical().noquote() << QStringLiteral("Не удалось открыть порт %1: %2")
                                     .arg(port)
                                     .arg(m_error);
        return false;
    }

    qInfo().noquote() << QStringLiteral("Сервер слушает %1:%2 (%3)")
                             .arg(m_server->serverAddress().toString())
                             .arg(m_server->serverPort())
                             .arg(m_tlsEnabled ? QStringLiteral("TLS") : QStringLiteral("без TLS"));

    qInfo().noquote() << QStringLiteral("Адрес на этом компьютере: 127.0.0.1:%1")
                             .arg(m_server->serverPort());

    const QStringList addresses = lanAddresses();
    for (const QString &address : addresses) {
        qInfo().noquote() << QStringLiteral("Адрес в локальной сети:   %1:%2")
                                 .arg(address)
                                 .arg(m_server->serverPort());
    }

    if (addresses.isEmpty()) {
        qInfo().noquote()
            << QStringLiteral("Сетевые адреса не найдены: другие компьютеры смогут "
                              "подключиться только при наличии сети.");
    }

    return true;
}

void ServerApp::stop()
{
    if (!m_server)
        return;

    if (!m_server->isListening() && m_sessions.isEmpty())
        return;

    m_server->close();

    // Удаляем сессии явно: их деструкторы снимают регистрацию в ядре и рвут сокеты.
    const QSet<ClientSession *> sessions = m_sessions;
    m_sessions.clear();
    for (ClientSession *session : sessions) {
        delete session;
    }
}

quint16 ServerApp::port() const
{
    if (!m_server || !m_server->isListening())
        return 0;
    return m_server->serverPort();
}

void ServerApp::onNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        auto *session = new ClientSession(socket, m_core, this);
        m_sessions.insert(session);

        // Соединяем удаление из учёта с фактическим удалением объекта.
        connect(session, &QObject::destroyed, this, [this, session]() {
            m_sessions.remove(session);
        });

        qInfo().noquote() << QStringLiteral("Подключение %1 (соединений: %2)")
                                 .arg(session->peerAddress())
                                 .arg(m_sessions.size());
    }
}

void ServerApp::attachSession(QSslSocket *socket)
{
    if (!socket)
        return;

    auto *session = new ClientSession(socket, m_core, this);
    m_sessions.insert(session);

    connect(session, &QObject::destroyed, this, [this, session]() {
        m_sessions.remove(session);
    });

    qInfo().noquote() << QStringLiteral("Защищённое подключение %1 (соединений: %2)")
                             .arg(session->peerAddress())
                             .arg(m_sessions.size());
}

} // namespace server
