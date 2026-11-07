#pragma once

#include <QHash>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

namespace server {

class ClientSession;
class ServerCore;

// Серверный процесс: слушает порт и создаёт сессию на каждое подключение.
class ServerApp : public QObject
{
    Q_OBJECT

public:
    explicit ServerApp(QObject *parent = nullptr);
    ~ServerApp() override;

    bool initialize(const QString &databasePath, const QString &attachmentsRoot, QString *error);
    bool start(quint16 port);
    void stop();

    quint16 port() const;
    int connectionCount() const { return m_sessions.size(); }
    QString errorString() const { return m_error; }

    ServerCore *core() const { return m_core; }

private slots:
    void onNewConnection();

private:
    QTcpServer *m_server = nullptr;
    ServerCore *m_core = nullptr;
    QHash<QTcpSocket *, ClientSession *> m_sessions;
    QString m_error;
};

} // namespace server
