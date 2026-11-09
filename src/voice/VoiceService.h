#pragma once

#include <QAudioFormat>
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

class SettingsStore;

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

    VoiceService(net::ClientConnection *connection, SettingsStore *settings = nullptr,
                 QObject *parent = nullptr);
    ~VoiceService() override;

    void call(qint64 chatId);
    void accept(qint64 chatId, qint64 callerId);
    void decline(qint64 chatId, qint64 callerId, const QString &reason = QString());
    void cancel(qint64 chatId);
    void hangup();

    void setMuted(bool muted);
    bool isMuted() const { return m_muted; }

    void setPushToTalk(bool enabled);
    bool pushToTalk() const { return m_pushToTalk; }
    void setTransmitting(bool transmitting);

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
    void callMissed(qint64 chatId, qint64 callerId, const QString &callerName);
    void speakingChanged(qint64 chatId, qint64 userId, bool speaking);

private slots:
    void onAudioReady();
    void onUdpDatagrams();
    void onSocketDisconnected();
    void onEvent(const QString &name, const QJsonObject &data);
    void flushJitter();

private:
    void startRoom(qint64 chatId);
    void beginE2ee(const QJsonObject &data);
    void applyPeerKey(qint64 userId, const QString &pubkeyBase64);
    void openAudio();
    void closeAudio();
    void openUdp(quint16 port, quint32 token, const QByteArray &key);
    void closeUdp();
    void sendDatagram(const QByteArray &payload);
    void setState(State state);
    void updateSpeaking(const QByteArray &pcm);
    void startCallTimer();
    void stopCallTimer();

    net::ClientConnection *m_connection = nullptr;
    SettingsStore *m_settings = nullptr;
    OpusCodec *m_codec = nullptr;

    QAudioInput *m_input = nullptr;
    QAudioOutput *m_output = nullptr;
    QIODevice *m_inputDevice = nullptr;
    QIODevice *m_outputDevice = nullptr;
    QAudioFormat m_inputFormat;
    QAudioFormat m_outputFormat;

    QUdpSocket *m_udp = nullptr;
    QTimer *m_keepalive = nullptr;
    QTimer *m_jitterTimer = nullptr;
    QTimer *m_callTimer = nullptr;

    QHostAddress m_serverAddress;
    quint16 m_serverVoicePort = 0;
    quint32 m_token = 0;
    QByteArray m_key;
    QByteArray m_roomKey;
    QByteArray m_privateKey;
    QByteArray m_publicKey;
    int m_participants = 2;
    QHash<qint64, QString> m_pendingPeerKeys;
    QByteArray m_senderKey;
    QHash<qint64, QByteArray> m_peerSenderKeys;
    QHash<qint64, QByteArray> m_pairwiseKeys;
    QHash<quint32, quint32> m_lastSequenceByUser;

    QByteArray m_captureBuffer;
    QMap<quint64, QByteArray> m_jitter;
    int m_packetsSent = 0;
    int m_packetsReceived = 0;
    qint64 m_captureBytes = 0;
    qint64 m_outputBytes = 0;
    int m_readyReadCount = 0;
    QTimer *m_audioWatch = nullptr;
    int m_watchTicks = 0;
    int m_watchLastReadyRead = 0;
    bool m_jitterPrimed = false;
    quint32 m_sendSequence = 0;

    State m_state = State::Idle;
    qint64 m_chatId = 0;
    qint64 m_callerId = 0;
    QString m_callerName;
    bool m_muted = false;
    bool m_speaking = false;
    bool m_pushToTalk = false;
    bool m_transmitting = true;
    qint64 m_lastLoudMs = 0;
};

} // namespace app

Q_DECLARE_METATYPE(app::VoiceService::State)
