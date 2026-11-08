#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QAudioInput;
class QAudioOutput;
class QIODevice;
class OpusCodec;

namespace net {
class ClientConnection;
}

namespace app {

// Голосовой канал: захват микрофона → Opus → серверный релей → Opus → динамик.
class VoiceService : public QObject
{
    Q_OBJECT

public:
    VoiceService(net::ClientConnection *connection, QObject *parent = nullptr);
    ~VoiceService() override;

    void start(qint64 chatId);
    void stop();

    void setMuted(bool muted);
    bool isMuted() const { return m_muted; }

    bool isActive() const { return m_active; }
    qint64 chatId() const { return m_chatId; }

    static bool isSupported(QString *error = nullptr);

signals:
    void activeChanged(bool active);
    void mutedChanged(bool muted);
    void errorOccurred(const QString &message);
    void peerJoined(qint64 chatId, qint64 userId);
    void peerLeft(qint64 chatId, qint64 userId);

private slots:
    void onAudioReady();
    void onVoiceFrame(quint32 chatId, quint32 userId, quint32 sequence, const QByteArray &data);
    void onSocketDisconnected();
    void onEvent(const QString &name, const QJsonObject &data);

private:
    void openAudio();
    void closeAudio();

    net::ClientConnection *m_connection = nullptr;
    OpusCodec *m_codec = nullptr;

    QAudioInput *m_input = nullptr;
    QAudioOutput *m_output = nullptr;
    QIODevice *m_inputDevice = nullptr;
    QIODevice *m_outputDevice = nullptr;

    QByteArray m_captureBuffer;
    QByteArray m_playbackBuffer;

    qint64 m_chatId = 0;
    quint32 m_sequence = 0;
    bool m_active = false;
    bool m_muted = false;
};

} // namespace app
