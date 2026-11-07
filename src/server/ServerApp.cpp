#include "server/ServerApp.h"

#include <QDebug>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace server {

ServerApp::ServerApp(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &ServerApp::onNewConnection);
}

ServerApp::~ServerApp()
{
    stop();
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

    const auto sockets = m_clients.keys();
    for (QTcpSocket *socket : sockets) {
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    m_clients.clear();
    m_server->close();
}

quint16 ServerApp::port() const
{
    return m_server->isListening() ? m_server->serverPort() : 0;
}

void ServerApp::onNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        const QString peer = QStringLiteral("%1:%2")
                                 .arg(socket->peerAddress().toString())
                                 .arg(socket->peerPort());
        m_clients.insert(socket, peer);
        qInfo().noquote() << QStringLiteral("Подключение %1 (всего: %2)")
                                 .arg(peer)
                                 .arg(m_clients.size());

        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            const QString address = m_clients.value(socket);
            m_clients.remove(socket);
            qInfo().noquote() << QStringLiteral("Отключение %1 (осталось: %2)")
                                     .arg(address)
                                     .arg(m_clients.size());
            socket->deleteLater();
        });
    }
}

} // namespace server
