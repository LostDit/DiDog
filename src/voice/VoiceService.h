#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>

class QAudioInput;
class QAudioOutput;
class QIODevice;
class QTcpSocket;
class QTimer;
class QUdpSocket;
class OpusCodec;

namespace net {
class ClientConnection;
}

namespace app {

// Голосовой канал: вызов (invite/accept/decline), Opus и UDP-релей через сервер.
class VoiceService : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,
        Calling,  // исходящий вызов, ждём ответа
        Ringing,  // входящий вызов
        Active    // разговор
    };
    Q_ENUM(State)

    VoiceService(net::ClientConnection *connection, QObject *parent = nullptr);
    ~VoiceService() override;

    void call(qint64 chatId);
    void accept(qint64 chatId, qint64 callerId);
    void decline(qint64 chatId, qint64 callerId);
    void cancel(qint64 chatId);
    void hangup();

    void setMuted(bool muted);
    bool isMuted() const { return m_muted; }

    State state() const { return m_state; }
    bool isActive() const { return m_state == State::Active; }
    qint64 chatId() const { return m_chatId; }

    static bool isSupported(QString *error = nullptr);

signals:
    void stateChanged(app::VoiceService::State state);
    void mutedChanged(bool muted);
    void errorOccurred(const QString &message);
    void incomingCall(qint64 chatId, qint64 callerId, const QString &callerName);
    void callAccepted(qint64 chatId, qint64 userId);
    void callDeclined(qint64 chatId, qint64 userId);
    void callCancelled(qint64 chatId);
    void speakingChanged(qint64 chatId, qint64 userId, bool speaking);

private slots:
    void onAudioReady();
    void onUdpDatagrams();
    void onSocketDisconnected();
    void onEvent(const QString &name, const QJsonObject &data);
    void flushJitter();

private:
    void startRoom(qint64 chatId);
    void openAudio();
    void closeAudio();
    void openUdp(quint16 port, quint32 token);
    void closeUdp();
    void sendDatagram(const QByteArray &payload);
    void setState(State state);
    void updateSpeaking(const QByteArray &pcm);

    net::ClientConnection *m_connection = nullptr;
    OpusCodec *m_codec = nullptr;

    QAudioInput *m_input = nullptr;
    QAudioOutput *m_output = nullptr;
    QIODevice *m_inputDevice = nullptr;
    QIODevice *m_outputDevice = nullptr;

    QUdpSocket *m_udp = nullptr;
    QTimer *m_keepalive = nullptr;
    QTimer *m_jitterTimer = nullptr;

    QHostAddress m_serverAddress;
    quint16 m_serverVoicePort = 0;
    quint32 m_token = 0;

    QByteArray m_captureBuffer;
    QMap<quint32, QByteArray> m_jitter;
    quint32 m_expectedSequence = 0;
    quint32 m_sendSequence = 0;

    State m_state = State::Idle;
    qint64 m_chatId = 0;
    qint64 m_callerId = 0;
    bool m_muted = false;
    bool m_speaking = false;
    qint64 m_lastLoudMs = 0;
};

} // namespace app

Q_DECLARE_METATYPE(app::VoiceService::State)
