#pragma once

#include <QSslConfiguration>
#include <QTcpServer>

class QSslSocket;

namespace server {

// TCP-сервер с TLS: принимает соединение, поднимает handshake и отдаёт
// уже зашифрованный сокет сессии.
class TlsServer : public QTcpServer
{
    Q_OBJECT

public:
    explicit TlsServer(const QSslConfiguration &configuration, QObject *parent = nullptr);

signals:
    void securedConnection(QSslSocket *socket);

protected:
    void incomingConnection(qintptr handle) override;

private:
    QSslConfiguration m_configuration;
};

} // namespace server
