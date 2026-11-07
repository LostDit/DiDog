#include "server/TlsServer.h"

#include <QDebug>
#include <QSslSocket>

namespace server {

TlsServer::TlsServer(const QSslConfiguration &configuration, QObject *parent)
    : QTcpServer(parent)
    , m_configuration(configuration)
{
}

void TlsServer::incomingConnection(qintptr handle)
{
    auto *socket = new QSslSocket(this);

    if (!socket->setSocketDescriptor(handle)) {
        delete socket;
        return;
    }

    socket->setSslConfiguration(m_configuration);

    // Клиентские сертификаты не требуются: проверка подлинности сервера —
    // на стороне клиента по отпечатку.
    connect(socket, QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),
            socket, [socket](const QList<QSslError> &errors) {
                Q_UNUSED(errors)
                socket->ignoreSslErrors();
            });

    connect(socket, &QSslSocket::encrypted, this, [this, socket]() {
        emit securedConnection(socket);
    });

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(socket, &QSslSocket::errorOccurred, this,
            [socket](QAbstractSocket::SocketError) {
                qWarning().noquote() << QStringLiteral("TLS handshake: %1")
                                            .arg(socket->errorString());
            });
#else
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, [socket](QAbstractSocket::SocketError) {
                qWarning().noquote() << QStringLiteral("TLS handshake: %1")
                                            .arg(socket->errorString());
            });
#endif

    connect(socket, &QSslSocket::disconnected, this, [this, socket]() {
        if (socket->parent() == this)
            socket->deleteLater();
    });

    socket->startServerEncryption();
}

} // namespace server
