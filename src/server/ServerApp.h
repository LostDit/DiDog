#pragma once

#include <QHash>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

namespace server {

// Точка входа серверного процесса: слушает порт и ведёт учёт подключений.
// Обработка протокола добавляется на этапе M2 (ClientSession).
class ServerApp : public QObject
{
    Q_OBJECT

public:
    explicit ServerApp(QObject *parent = nullptr);
    ~ServerApp() override;

    bool start(quint16 port);
    void stop();

    quint16 port() const;
    int connectionCount() const { return m_clients.size(); }
    QString errorString() const { return m_error; }

private slots:
    void onNewConnection();

private:
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, QString> m_clients;
    QString m_error;
};

} // namespace server
