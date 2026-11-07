#pragma once

#include <QObject>
#include <QSet>
#include <QString>

class QTcpServer;

namespace server {

class ClientSession;
class ServerCore;

// Серверный процесс: слушает порт и создаёт сессию на каждое подключение.
// Владение: сессии удаляются до ядра, иначе их деструкторы обращаются
// к уже закрытой базе.
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
    QSet<ClientSession *> m_sessions;
    QString m_error;
};

} // namespace server
