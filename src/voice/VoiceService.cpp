#include "voice/VoiceService.h"

#include "core/Protocol.h"
#include "net/ClientConnection.h"
#include "voice/OpusCodec.h"

#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QAudioInput>
#include <QAudioOutput>
#include <QDebug>
#include <QJsonObject>

#include <algorithm>

namespace app {
namespace {

constexpr int kSampleRate = 48000;
constexpr int kFrameBytes = 960 * 2; // 20 мс, 16 бит моно
constexpr int kMaxPlaybackBuffer = kFrameBytes * 6;

QAudioFormat voiceFormat()
{
    QAudioFormat format;
    format.setSampleRate(kSampleRate);
    format.setChannelCount(1);
    format.setSampleSize(16);
    format.setCodec(QStringLiteral("audio/pcm"));
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);
    return format;
}

} // namespace

VoiceService::VoiceService(net::ClientConnection *connection, QObject *parent)
    : QObject(parent)
    , m_connection(connection)
    , m_codec(new OpusCodec)
{
    if (!m_connection)
        return;

    connect(m_connection, &net::ClientConnection::voiceFrameReceived,
            this, &VoiceService::onVoiceFrame);
    connect(m_connection, &net::ClientConnection::socketDisconnected,
            this, &VoiceService::onSocketDisconnected);
    connect(m_connection, &net::ClientConnection::eventReceived,
            this, &VoiceService::onEvent);
}

VoiceService::~VoiceService()
{
    if (m_active && m_connection && m_connection->isConnected()) {
        m_connection->request(QStringLiteral("voice.leave"),
                              QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(m_chatId)}},
                              nullptr, 1000);
    }

    closeAudio();
    delete m_codec;
}

bool VoiceService::isSupported(QString *error)
{
    return OpusCodec::isAvailable(error);
}

void VoiceService::start(qint64 chatId)
{
    if (m_active && m_chatId == chatId)
        return;

    if (m_active)
        stop();

    if (!m_connection || !m_connection->isConnected()) {
        emit errorOccurred(tr("Нет соединения с сервером."));
        return;
    }

    QString codecError;
    if (!m_codec->open(&codecError)) {
        emit errorOccurred(codecError);
        return;
    }

    m_connection->request(
        QStringLiteral("voice.join"),
        QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(chatId)}},
        [this, chatId](const QJsonObject &reply) {
            if (!reply.value(QStringLiteral("ok")).toBool(false)) {
                emit errorOccurred(core::proto::errorMessageOf(reply));
                return;
            }

            m_chatId = chatId;
            m_active = true;
            m_sequence = 0;
            m_captureBuffer.clear();
            m_playbackBuffer.clear();
            openAudio();
            emit activeChanged(true);
        },
        10000);
}

void VoiceService::stop()
{
    if (!m_active)
        return;

    m_active = false;

    if (m_connection && m_connection->isConnected()) {
        m_connection->request(
            QStringLiteral("voice.leave"),
            QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(m_chatId)}}, nullptr,
            2000);
    }

    closeAudio();
    m_chatId = 0;
    emit activeChanged(false);
}

void VoiceService::setMuted(bool muted)
{
    if (m_muted == muted)
        return;

    m_muted = muted;
    if (muted)
        m_captureBuffer.clear();

    emit mutedChanged(muted);
}

void VoiceService::openAudio()
{
    const QAudioFormat format = voiceFormat();
    const QAudioDeviceInfo inputDevice = QAudioDeviceInfo::defaultInputDevice();
    const QAudioDeviceInfo outputDevice = QAudioDeviceInfo::defaultOutputDevice();

    if (inputDevice.isNull() || outputDevice.isNull()) {
        emit errorOccurred(tr("Микрофон или динамики не найдены."));
        return;
    }

    m_input = new QAudioInput(inputDevice, format, this);
    m_inputDevice = m_input->start();
    if (!m_inputDevice) {
        emit errorOccurred(tr("Не удалось открыть микрофон."));
        return;
    }
    connect(m_inputDevice, &QIODevice::readyRead, this, &VoiceService::onAudioReady);

    m_output = new QAudioOutput(outputDevice, format, this);
    m_outputDevice = m_output->start();
}

void VoiceService::closeAudio()
{
    if (m_input) {
        m_input->stop();
        m_input->deleteLater();
        m_input = nullptr;
        m_inputDevice = nullptr;
    }

    if (m_output) {
        m_output->stop();
        m_output->deleteLater();
        m_output = nullptr;
        m_outputDevice = nullptr;
    }
}

void VoiceService::onAudioReady()
{
    if (!m_inputDevice || !m_active)
        return;

    m_captureBuffer.append(m_inputDevice->readAll());

    while (m_captureBuffer.size() >= kFrameBytes) {
        const QByteArray frame = m_captureBuffer.left(kFrameBytes);
        m_captureBuffer.remove(0, kFrameBytes);

        if (m_muted)
            continue;

        const QByteArray packet = m_codec->encode(frame);
        if (packet.isEmpty())
            continue;

        m_connection->sendVoiceFrame(static_cast<quint32>(m_chatId), m_sequence++, packet);
    }
}

void VoiceService::onVoiceFrame(quint32 chatId, quint32 userId, quint32 sequence,
                                const QByteArray &data)
{
    Q_UNUSED(sequence)

    if (!m_active || static_cast<qint64>(chatId) != m_chatId || !m_outputDevice)
        return;

    const QByteArray pcm = m_codec->decode(data);
    if (pcm.isEmpty())
        return;

    m_playbackBuffer.append(pcm);
    if (m_playbackBuffer.size() > kMaxPlaybackBuffer)
        m_playbackBuffer = m_playbackBuffer.right(kMaxPlaybackBuffer);

    if (m_output->bytesFree() > 0 && !m_playbackBuffer.isEmpty()) {
        m_outputDevice->write(m_playbackBuffer);
        m_playbackBuffer.clear();
    }

    Q_UNUSED(userId)
}

void VoiceService::onSocketDisconnected()
{
    if (!m_active)
        return;

    m_active = false;
    closeAudio();
    m_chatId = 0;
    emit activeChanged(false);
}

void VoiceService::onEvent(const QString &name, const QJsonObject &data)
{
    if (name != QLatin1String(core::proto::event::kVoice))
        return;

    const qint64 chatId = static_cast<qint64>(
        data.value(QStringLiteral("chat_id")).toDouble());
    const qint64 userId = static_cast<qint64>(
        data.value(QStringLiteral("user_id")).toDouble());
    const bool joined = data.value(QStringLiteral("joined")).toBool(false);

    if (joined)
        emit peerJoined(chatId, userId);
    else
        emit peerLeft(chatId, userId);
}

} // namespace app
