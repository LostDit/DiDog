#include "server/ServerApp.h"

#include "server/ClientSession.h"
#include "server/ServerCore.h"

#include <QDebug>
#include <QHostAddress>
#include <QNetworkInterface>
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
    , m_server(new QTcpServer(this))
    , m_core(new ServerCore(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &ServerApp::onNewConnection);
}

ServerApp::~ServerApp()
{
    stop();
}

bool ServerApp::initialize(const QString &databasePath, const QString &attachmentsRoot,
                           QString *error)
{
    return m_core->initialize(databasePath, attachmentsRoot, error);
}

bool ServerApp::start(quint16 port)
{
    if (m_server->isListening())
        return true;

    if (!m_server->listen(QHostAddress::Any, port)) {
        m_error = m_server->errorString();
        qCritical().noquote() << QStringLiteral("Не удалось открыть порт %1: %2")
                                     .arg(port)
                                     .arg(m_error);
        return false;
    }

    qInfo().noquote() << QStringLiteral("Сервер слушает %1:%2")
                             .arg(m_server->serverAddress().toString())
                             .arg(m_server->serverPort());
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
    return m_server->isListening() ? m_server->serverPort() : 0;
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

} // namespace server
