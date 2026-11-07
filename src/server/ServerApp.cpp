#include "server/ServerApp.h"

#include "server/ClientSession.h"
#include "server/ServerCore.h"

#include <QDebug>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace server {

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
    return true;
}

void ServerApp::stop()
{
    if (!m_server->isListening())
        return;

    const auto sessions = m_sessions.values();
    for (ClientSession *session : sessions) {
        if (m_core)
            m_core->unregisterSession(session);
        session->deleteLater();
    }

    m_sessions.clear();
    m_server->close();
}

quint16 ServerApp::port() const
{
    return m_server->isListening() ? m_server->serverPort() : 0;
}

void ServerApp::onNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        auto *session = new ClientSession(socket, m_core, this);
        m_sessions.insert(socket, session);

        qInfo().noquote() << QStringLiteral("Подключение %1 (соединений: %2)")
                                 .arg(session->peerAddress())
                                 .arg(m_sessions.size());

        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_sessions.remove(socket);
        });
    }
}

} // namespace server
