#pragma once

#include <QObject>
#include <QSet>
#include <QSslConfiguration>
#include <QString>

class QTcpServer;
class QSslSocket;

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

    // Включает TLS: сертификат и ключ в PEM.
    bool enableTls(const QString &certificatePath, const QString &keyPath, QString *error);
    bool tlsEnabled() const { return m_tlsEnabled; }

    bool initialize(const QString &databasePath, const QString &attachmentsRoot, QString *error);
    bool start(quint16 port);
    void stop();

    // UDP-порт голосового релея (по умолчанию 45001).
    void setVoicePort(quint16 port) { m_voicePort = port; }

    // Администрирование: создание промокода (CLI --add-promo).
    bool createPromo(const QString &code, const QString &plan, int days, int maxUses,
                     QString *error);

    quint16 port() const;
    int connectionCount() const { return m_sessions.size(); }
    QString errorString() const { return m_error; }

    ServerCore *core() const { return m_core; }

private slots:
    void onNewConnection();

private:
    void createServer();
    void attachSession(QSslSocket *socket);

    QTcpServer *m_server = nullptr;
    ServerCore *m_core = nullptr;
    QSet<ClientSession *> m_sessions;
    QString m_error;

    bool m_tlsEnabled = false;
    QSslConfiguration m_tlsConfiguration;
    quint16 m_voicePort = 45001;
};

} // namespace server
