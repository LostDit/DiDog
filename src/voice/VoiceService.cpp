#include "voice/VoiceService.h"

#include "app/SettingsStore.h"
#include "core/Protocol.h"
#include "net/ClientConnection.h"
#include "voice/OpusCodec.h"
#include "voice/VoiceCrypto.h"

#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QAudioInput>
#include <QAudioOutput>
#include <QDateTime>
#include <QDebug>
#include <QHostInfo>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>
#include <QVector>

namespace {

// Приведение PCM между форматом устройства и кодеком (48 кГц, моно, Int16).
QByteArray convertPcm(const QByteArray &input, const QAudioFormat &from,
                      const QAudioFormat &to)
{
    if (input.isEmpty() || !from.isValid() || !to.isValid() || from == to)
        return input;
    if (from.sampleType() != QAudioFormat::SignedInt || from.sampleSize() != 16
        || to.sampleType() != QAudioFormat::SignedInt || to.sampleSize() != 16) {
        return input;
    }

    const int fromChannels = qMax(1, from.channelCount());
    const int toChannels = qMax(1, to.channelCount());
    const qint64 frames = input.size() / (2 * fromChannels);
    if (frames <= 0)
        return QByteArray();

    QVector<float> mono;
    mono.reserve(static_cast<int>(frames));
    const auto *samples = reinterpret_cast<const qint16 *>(input.constData());
    for (qint64 i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int channel = 0; channel < fromChannels; ++channel)
            sum += samples[i * fromChannels + channel] / 32768.0f;
        mono.append(sum / fromChannels);
    }

    const double ratio = static_cast<double>(to.sampleRate()) / from.sampleRate();
    const qint64 outFrames = qMax<qint64>(1, static_cast<qint64>(mono.size() * ratio));

    QByteArray output;
    output.resize(static_cast<int>(outFrames * toChannels * 2));
    auto *out = reinterpret_cast<qint16 *>(output.data());

    for (qint64 i = 0; i < outFrames; ++i) {
        const double source = i / ratio;
        const qint64 index = static_cast<qint64>(source);
        const double fraction = source - index;
        const float first = mono.at(qMin<qint64>(index, mono.size() - 1));
        const float second = mono.at(qMin<qint64>(index + 1, mono.size() - 1));
        const float value = first + (second - first) * static_cast<float>(fraction);
        for (int channel = 0; channel < toChannels; ++channel)
            out[i * toChannels + channel] =
                static_cast<qint16>(qBound(-1.0f, value, 1.0f) * 32767.0f);
    }

    return output;
}

} // namespace

namespace app {
namespace {

constexpr int kSampleRate = 48000;
constexpr int kFrameBytes = 960 * 2; // 20 мс, 16 бит моно
constexpr int kMaxJitterPackets = 8;

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

VoiceService::VoiceService(net::ClientConnection *connection, SettingsStore *settings,
                           QObject *parent)
    : QObject(parent)
    , m_connection(connection)
    , m_settings(settings)
    , m_codec(new OpusCodec)
    , m_keepalive(new QTimer(this))
    , m_jitterTimer(new QTimer(this))
{
    m_pushToTalk = m_settings && m_settings->voicePushToTalk();
    m_transmitting = !m_pushToTalk;

    if (!m_connection)
        return;

    connect(m_connection, &net::ClientConnection::voiceFrameReceived,
            this, [](quint32, quint32, quint32, const QByteArray &) {});
    connect(m_connection, &net::ClientConnection::socketDisconnected,
            this, &VoiceService::onSocketDisconnected);
    connect(m_connection, &net::ClientConnection::eventReceived,
            this, &VoiceService::onEvent);

    m_keepalive->setInterval(5000);
    connect(m_keepalive, &QTimer::timeout, this, [this]() {
        if (m_udp && m_token != 0)
            sendDatagram(QByteArray());
    });

    m_jitterTimer->setInterval(20);
    connect(m_jitterTimer, &QTimer::timeout, this, &VoiceService::flushJitter);

    m_audioWatch = new QTimer(this);
    m_audioWatch->setInterval(1000);
    connect(m_audioWatch, &QTimer::timeout, this, [this]() {
        if (m_state != State::Active)
            return;
        ++m_watchTicks;
        if (m_inputDevice && m_readyReadCount == m_watchLastReadyRead) {
            qWarning("VOICE: микрофон не отдаёт данные — перезапуск");
            m_input->stop();
            m_inputDevice = m_input->start();
            if (m_inputDevice)
                connect(m_inputDevice, &QIODevice::readyRead, this, &VoiceService::onAudioReady);
        }
        m_watchLastReadyRead = m_readyReadCount;
        if (m_watchTicks % 5 == 0) {
            qInfo("VOICE: захват=%lld Б, воспроизведено=%lld Б, readyRead=%d, отправлено=%d, получено=%d",
                  m_captureBytes, m_outputBytes, m_readyReadCount, m_packetsSent,
                  m_packetsReceived);
        }
    });

    m_callTimer = new QTimer(this);
    m_callTimer->setSingleShot(true);
    m_callTimer->setInterval(30000);
    connect(m_callTimer, &QTimer::timeout, this, [this]() {
        if (m_state == State::Calling) {
            if (m_connection && m_connection->isConnected()) {
                m_connection->request(
                    QStringLiteral("voice.cancel"),
                    QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(m_chatId)}},
                    nullptr, 3000);
            }
            closeAudio();
            closeUdp();
            m_chatId = 0;
            m_callerId = 0;
            setState(State::Idle);
            emit errorOccurred(tr("Абонент не ответил."));
            return;
        }

        if (m_state == State::Ringing) {
            if (m_connection && m_connection->isConnected()) {
                m_connection->request(
                    QStringLiteral("voice.decline"),
                    QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(m_chatId)},
                                {QStringLiteral("caller_id"), static_cast<double>(m_callerId)},
                                {QStringLiteral("reason"), QStringLiteral("timeout")}},
                    nullptr, 3000);
            }

            const qint64 chatId = m_chatId;
            const qint64 callerId = m_callerId;
            const QString callerName = m_callerName;
            m_chatId = 0;
            m_callerId = 0;
            m_callerName.clear();
            setState(State::Idle);
            emit callMissed(chatId, callerId, callerName);
        }
    });
}

VoiceService::~VoiceService()
{
    if (m_connection && m_connection->isConnected() && m_state != State::Idle) {
        m_connection->request(QStringLiteral("voice.leave"),
                              QJsonObject{{QStringLiteral("chat_id"),
                                           static_cast<double>(m_chatId)}},
                              nullptr, 1000);
    }

    closeAudio();
    closeUdp();
    delete m_codec;
}

bool VoiceService::isSupported(QString *error)
{
    return OpusCodec::isAvailable(error);
}

void VoiceService::setState(State state)
{
    if (m_state == state)
        return;

    m_state = state;
    emit stateChanged(state);
}

void VoiceService::call(qint64 chatId)
{
    if (!m_connection || !m_connection->isConnected()) {
        emit errorOccurred(tr("Нет соединения с сервером."));
        return;
    }
    if (chatId <= 0)
        return;

    if (m_state != State::Idle)
        hangup();

    if (m_settings)
        m_codec->setBitrate(m_settings->voiceBitrate());

    QString codecError;
    if (!m_codec->open(&codecError)) {
        emit errorOccurred(codecError);
        return;
    }

    m_chatId = chatId;
    m_callerId = 0;
    m_callerName.clear();
    setState(State::Calling);
    startCallTimer();

    m_connection->request(
        QStringLiteral("voice.join"),
        QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(chatId)}},
        [this](const QJsonObject &reply) {
            if (!reply.value(QStringLiteral("ok")).toBool(false)) {
                emit errorOccurred(core::proto::errorMessageOf(reply));
                setState(State::Idle);
                return;
            }

            const QJsonObject data = reply.value(QStringLiteral("data")).toObject();
            const QByteArray key = QByteArray::fromHex(
                data.value(QStringLiteral("voice_key")).toString().toLatin1());
            if (key.size() != 32) {
                emit errorOccurred(tr("Сервер не выдал ключ шифрования голоса."));
                setState(State::Idle);
                return;
            }
            openUdp(static_cast<quint16>(data.value(QStringLiteral("udp_port")).toInt()),
                    static_cast<quint32>(data.value(QStringLiteral("udp_token")).toDouble()),
                    key);
            if (!m_settings || m_settings->voiceE2ee())
                beginE2ee(data);
            else
                qInfo("VOICE: E2EE отключён настройкой, используется ключ комнаты");
            openAudio();

            m_connection->request(
                QStringLiteral("voice.invite"),
                QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(m_chatId)}},
                [this](const QJsonObject &inviteReply) {
                    if (!inviteReply.value(QStringLiteral("ok")).toBool(false))
                        emit errorOccurred(core::proto::errorMessageOf(inviteReply));
                },
                5000);
        },
        10000);
}

void VoiceService::accept(qint64 chatId, qint64 callerId)
{
    if (!m_connection || !m_connection->isConnected()) {
        emit errorOccurred(tr("Нет соединения с сервером."));
        return;
    }

    if (m_settings)
        m_codec->setBitrate(m_settings->voiceBitrate());

    QString codecError;
    if (!m_codec->open(&codecError)) {
        emit errorOccurred(codecError);
        return;
    }

    m_chatId = chatId;
    m_callerId = callerId;
    setState(State::Active);
    stopCallTimer();

    m_connection->request(
        QStringLiteral("voice.join"),
        QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(chatId)}},
        [this, chatId, callerId](const QJsonObject &reply) {
            if (!reply.value(QStringLiteral("ok")).toBool(false)) {
                emit errorOccurred(core::proto::errorMessageOf(reply));
                setState(State::Idle);
                return;
            }

            const QJsonObject data = reply.value(QStringLiteral("data")).toObject();
            const QByteArray key = QByteArray::fromHex(
                data.value(QStringLiteral("voice_key")).toString().toLatin1());
            if (key.size() != 32) {
                emit errorOccurred(tr("Сервер не выдал ключ шифрования голоса."));
                setState(State::Idle);
                return;
            }
            openUdp(static_cast<quint16>(data.value(QStringLiteral("udp_port")).toInt()),
                    static_cast<quint32>(data.value(QStringLiteral("udp_token")).toDouble()),
                    key);
            if (!m_settings || m_settings->voiceE2ee())
                beginE2ee(data);
            else
                qInfo("VOICE: E2EE отключён настройкой, используется ключ комнаты");
            openAudio();

            m_connection->request(
                QStringLiteral("voice.accept"),
                QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(chatId)},
                            {QStringLiteral("caller_id"), static_cast<double>(callerId)}},
                nullptr, 5000);
        },
        10000);
}

void VoiceService::decline(qint64 chatId, qint64 callerId, const QString &reason)
{
    stopCallTimer();

    if (m_connection && m_connection->isConnected()) {
        QJsonObject request{{QStringLiteral("chat_id"), static_cast<double>(chatId)},
                            {QStringLiteral("caller_id"), static_cast<double>(callerId)}};
        if (!reason.isEmpty())
            request.insert(QStringLiteral("reason"), reason);

        m_connection->request(QStringLiteral("voice.decline"), request, nullptr, 5000);
    }

    if (m_state == State::Ringing) {
        m_callerName.clear();
        setState(State::Idle);
    }
}

void VoiceService::cancel(qint64 chatId)
{
    stopCallTimer();

    if (m_connection && m_connection->isConnected()) {
        m_connection->request(
            QStringLiteral("voice.cancel"),
            QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(chatId)}},
            nullptr, 5000);
    }

    hangup();
}

void VoiceService::hangup()
{
    if (m_state == State::Idle)
        return;

    stopCallTimer();

    if (m_connection && m_connection->isConnected()) {
        m_connection->request(
            QStringLiteral("voice.leave"),
            QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(m_chatId)}},
            nullptr, 2000);
    }

    closeAudio();
    closeUdp();
    m_jitter.clear();
    m_jitterPrimed = false;
    m_chatId = 0;
    m_callerId = 0;
    m_callerName.clear();
    setState(State::Idle);
}

void VoiceService::setMuted(bool muted)
{
    if (m_muted == muted)
        return;

    m_muted = muted;
    if (muted) {
        m_captureBuffer.clear();
        if (m_speaking && m_connection && m_connection->isConnected()) {
            m_speaking = false;
            m_connection->request(
                QStringLiteral("voice.speaking"),
                QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(m_chatId)},
                            {QStringLiteral("on"), false}},
                nullptr, 3000);
        }
    }

    emit mutedChanged(muted);
}

void VoiceService::setPushToTalk(bool enabled)
{
    if (m_pushToTalk == enabled)
        return;

    m_pushToTalk = enabled;
    m_transmitting = !enabled;
}

void VoiceService::setTransmitting(bool transmitting)
{
    if (!m_pushToTalk || m_transmitting == transmitting)
        return;

    m_transmitting = transmitting;
}

void VoiceService::openAudio()
{
    qInfo("VOICE: открытие аудио");
    const QAudioFormat format = voiceFormat();

    QAudioDeviceInfo inputDevice = QAudioDeviceInfo::defaultInputDevice();
    QAudioDeviceInfo outputDevice = QAudioDeviceInfo::defaultOutputDevice();

    const QString inputName = m_settings ? m_settings->voiceInputDevice() : QString();
    if (!inputName.isEmpty()) {
        for (const QAudioDeviceInfo &device :
             QAudioDeviceInfo::availableDevices(QAudio::AudioInput)) {
            if (device.deviceName() == inputName) {
                inputDevice = device;
                break;
            }
        }
    }

    const QString outputName = m_settings ? m_settings->voiceOutputDevice() : QString();
    if (!outputName.isEmpty()) {
        for (const QAudioDeviceInfo &device :
             QAudioDeviceInfo::availableDevices(QAudio::AudioOutput)) {
            if (device.deviceName() == outputName) {
                outputDevice = device;
                break;
            }
        }
    }

    if (inputDevice.isNull() || outputDevice.isNull()) {
        emit errorOccurred(tr("Микрофон или динамики не найдены."));
        return;
    }

    m_inputFormat = format;
    if (!inputDevice.isFormatSupported(m_inputFormat))
        m_inputFormat = inputDevice.nearestFormat(format);
    if (!m_inputFormat.isValid())
        m_inputFormat = format;
    qInfo("VOICE: микрофон %s, %d Гц, каналов %d", qPrintable(inputDevice.deviceName()),
          m_inputFormat.sampleRate(), m_inputFormat.channelCount());

    m_input = new QAudioInput(inputDevice, m_inputFormat, this);
    m_inputDevice = m_input->start();
    qInfo("VOICE: состояние микрофона=%d, ошибка=%d", int(m_input->state()),
          int(m_input->error()));
    if (!m_inputDevice) {
        emit errorOccurred(tr("Не удалось открыть микрофон."));
        return;
    }
    connect(m_inputDevice, &QIODevice::readyRead, this, &VoiceService::onAudioReady);
    connect(m_input, &QAudioInput::stateChanged, this, [this](QAudio::State state) {
        qInfo("VOICE: состояние микрофона изменилось: %d", int(state));
        if (m_state == State::Active && (state == QAudio::IdleState || state == QAudio::SuspendedState))
            m_input->resume();
    });

    m_outputFormat = format;
    if (!outputDevice.isFormatSupported(m_outputFormat))
        m_outputFormat = outputDevice.nearestFormat(format);
    if (!m_outputFormat.isValid())
        m_outputFormat = format;
    qInfo("VOICE: динамики %s, %d Гц, каналов %d", qPrintable(outputDevice.deviceName()),
          m_outputFormat.sampleRate(), m_outputFormat.channelCount());

    m_output = new QAudioOutput(outputDevice, m_outputFormat, this);
    m_outputDevice = m_output->start();
    connect(m_output, &QAudioOutput::stateChanged, this, [this](QAudio::State state) {
        qInfo("VOICE: состояние динамиков изменилось: %d", int(state));
        if (m_state == State::Active && state == QAudio::SuspendedState)
            m_output->resume();
    });
    qInfo("VOICE: состояние динамиков=%d, ошибка=%d", int(m_output->state()),
          int(m_output->error()));
    m_jitterTimer->start();
    if (m_audioWatch)
        m_audioWatch->start();
}

void VoiceService::closeAudio()
{
    if (m_audioWatch)
        m_audioWatch->stop();
    m_captureBytes = 0;
    m_outputBytes = 0;
    m_readyReadCount = 0;
    m_watchTicks = 0;
    m_watchLastReadyRead = 0;
    if (m_packetsSent > 0 || m_packetsReceived > 0) {
        qInfo("VOICE: отправлено пакетов=%d, получено=%d", m_packetsSent,
              m_packetsReceived);
        m_packetsSent = 0;
        m_packetsReceived = 0;
    }
    m_jitterTimer->stop();

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

void VoiceService::beginE2ee(const QJsonObject &data)
{
    m_participants = data.value(QStringLiteral("participants")).toInt(2);
    qInfo("VOICE: вход в E2EE, участников=%d", m_participants);
    m_roomKey = m_key;
    m_pendingPeerKeys.clear();
    m_peerSenderKeys.clear();
    m_pairwiseKeys.clear();
    m_senderKey.clear();

    if (m_participants > 2) {
        m_senderKey.resize(32);
        QRandomGenerator::global()->fillRange(
            reinterpret_cast<quint32 *>(m_senderKey.data()), 8);
    }

    QString error;
    if (!VoiceCrypto::generateKeyPair(&m_privateKey, &m_publicKey, &error)) {
        emit errorOccurred(tr("Не удалось создать ключ E2EE: %1").arg(error));
        return;
    }

    const QJsonObject pubkeys = data.value(QStringLiteral("pubkeys")).toObject();
    for (auto it = pubkeys.constBegin(); it != pubkeys.constEnd(); ++it)
        applyPeerKey(it.key().toLongLong(), it.value().toString());

    if (m_connection) {
        m_connection->request(
            QStringLiteral("voice.pubkey"),
            QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(m_chatId)},
                        {QStringLiteral("pubkey"),
                         QString::fromLatin1(m_publicKey.toBase64())}},
            nullptr, 10000);
    }
}

void VoiceService::applyPeerKey(qint64 userId, const QString &pubkeyBase64)
{
    if (userId <= 0 || pubkeyBase64.isEmpty())
        return;

    if (m_privateKey.isEmpty()) {
        m_pendingPeerKeys.insert(userId, pubkeyBase64);
        return;
    }

    // Групповые звонки пока используют ключ комнаты (см. docs/VOICE-SECURITY.md).
    if (m_participants > 2)
        return;

    const QByteArray peerKey = QByteArray::fromBase64(pubkeyBase64.toLatin1());
    bool ok = false;
    const QByteArray secret = VoiceCrypto::deriveSharedSecret(m_privateKey, peerKey, &ok);
    if (!ok || secret.size() != 32)
        return;

    const QByteArray info = QByteArrayLiteral("chat-") + QByteArray::number(m_chatId);
    const QByteArray pairwiseKey =
        VoiceCrypto::hkdfSha256(secret, QByteArrayLiteral("didog-voice"), info);
    m_pairwiseKeys.insert(userId, pairwiseKey);
    qInfo("VOICE: получен ключ участника %lld", userId);

    if (m_participants <= 2) {
        m_key = pairwiseKey;
        return;
    }

    if (m_senderKey.isEmpty() || !m_connection)
        return;

    bool encrypted = false;
    const QByteArray blob = VoiceCrypto::encrypt(pairwiseKey, 0, m_senderKey, &encrypted);
    if (!encrypted)
        return;
    m_connection->request(
        QStringLiteral("voice.senderkey"),
        QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(m_chatId)},
                    {QStringLiteral("recipient_id"), static_cast<double>(userId)},
                    {QStringLiteral("payload"), QString::fromLatin1(blob.toBase64())}},
        nullptr, 10000);
}

void VoiceService::openUdp(quint16 port, quint32 token, const QByteArray &key)
{
    closeUdp();

    m_serverVoicePort = port;
    m_token = token;
    m_key = key;
    m_lastSequenceByUser.clear();

    const QString host = m_connection ? m_connection->host() : QString();
    if (!m_serverAddress.setAddress(host)) {
        const QHostInfo info = QHostInfo::fromName(host);
        for (const QHostAddress &address : info.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                m_serverAddress = address;
                break;
            }
        }
    }

    if (m_serverAddress.isNull() || port == 0 || token == 0) {
        emit errorOccurred(tr("Сервер не выдал UDP-адрес для голоса."));
        return;
    }

    m_udp = new QUdpSocket(this);
    m_udp->bind(QHostAddress(QHostAddress::Any), 0);
    connect(m_udp, &QUdpSocket::readyRead, this, &VoiceService::onUdpDatagrams);

    m_sendSequence = 0;
    sendDatagram(QByteArray()); // «привет»: сервер запоминает наш адрес
    m_keepalive->start();
}

void VoiceService::closeUdp()
{
    m_keepalive->stop();

    if (m_udp) {
        m_udp->close();
        m_udp->deleteLater();
        m_udp = nullptr;
    }

    m_token = 0;
    m_key.clear();
    m_roomKey.clear();
    m_privateKey.clear();
    m_publicKey.clear();
    m_pendingPeerKeys.clear();
    m_peerSenderKeys.clear();
    m_pairwiseKeys.clear();
    m_senderKey.clear();
    m_participants = 2;
    m_lastSequenceByUser.clear();
}

void VoiceService::sendDatagram(const QByteArray &payload)
{
    if (!m_udp || m_token == 0 || m_serverAddress.isNull() || m_serverVoicePort == 0)
        return;

    const quint32 sequence = m_sendSequence++;

    QByteArray body = payload;
    if (!payload.isEmpty()) {
        bool encrypted = false;
        const QByteArray outgoingKey =
            (m_participants > 2 && !m_senderKey.isEmpty()) ? m_senderKey : m_key;
        body = VoiceCrypto::encrypt(outgoingKey, sequence, payload, &encrypted);
        if (!encrypted)
            return;
    }

    if (!payload.isEmpty())
        ++m_packetsSent;

    const QByteArray datagram =
        core::proto::encodeVoiceDatagram(m_token, sequence, body);
    m_udp->writeDatagram(datagram, m_serverAddress, m_serverVoicePort);
}

void VoiceService::updateSpeaking(const QByteArray &pcm)
{
    if (pcm.size() < 2)
        return;

    const auto *samples = reinterpret_cast<const qint16 *>(pcm.constData());
    const int count = pcm.size() / 2;
    qint64 sum = 0;
    for (int i = 0; i < count; ++i)
        sum += qAbs(samples[i]);

    const int average = count > 0 ? static_cast<int>(sum / count) : 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (average > 500)
        m_lastLoudMs = now;

    bool shouldSpeak = !m_muted && average > 500;
    if (m_speaking && !shouldSpeak && now - m_lastLoudMs > 400)
        shouldSpeak = false;
    else if (m_speaking && !shouldSpeak)
        shouldSpeak = true; // гистерезис: не мигаем между слогами

    if (shouldSpeak == m_speaking)
        return;

    m_speaking = shouldSpeak;
    if (m_connection && m_connection->isConnected()) {
        m_connection->request(
            QStringLiteral("voice.speaking"),
            QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(m_chatId)},
                        {QStringLiteral("on"), m_speaking}},
            nullptr, 3000);
    }
}

void VoiceService::onAudioReady()
{
    if (!m_inputDevice || m_state != State::Active) {
        if (!m_inputDevice)
            qWarning("VOICE: нет устройства ввода");
        return;
    }

    const QByteArray raw = m_inputDevice->readAll();
    ++m_readyReadCount;
    m_captureBytes += raw.size();
    m_captureBuffer.append(convertPcm(raw, m_inputFormat, voiceFormat()));

    while (m_captureBuffer.size() >= kFrameBytes) {
        const QByteArray frame = m_captureBuffer.left(kFrameBytes);
        m_captureBuffer.remove(0, kFrameBytes);

        const bool canSend = !m_muted && (!m_pushToTalk || m_transmitting);

        if (canSend) {
            updateSpeaking(frame);
        } else if (m_speaking) {
            m_speaking = false;
            if (m_connection && m_connection->isConnected()) {
                m_connection->request(
                    QStringLiteral("voice.speaking"),
                    QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(m_chatId)},
                                {QStringLiteral("on"), false}},
                    nullptr, 3000);
            }
        }

        if (!canSend)
            continue;

        const QByteArray packet = m_codec->encode(frame);
        if (!packet.isEmpty())
            sendDatagram(packet);
    }
}

void VoiceService::onUdpDatagrams()
{
    if (!m_udp)
        return;

    while (m_udp->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_udp->receiveDatagram();

        quint32 userId = 0;
        quint32 sequence = 0;
        QByteArray payload;
        if (!core::proto::decodeVoiceRelay(datagram.data(), &userId, &sequence, &payload))
            continue;

        Q_UNUSED(userId)

        if (payload.isEmpty())
            continue;

        // Повтор или слишком старый пакет от этого участника — отбрасываем.
        const quint32 last = m_lastSequenceByUser.value(userId, 0);
        if (last != 0 && sequence <= last)
            continue;
        m_lastSequenceByUser.insert(userId, sequence);

        bool decrypted = false;
        QByteArray opus;
        if (m_participants > 2 && m_peerSenderKeys.contains(userId))
            opus = VoiceCrypto::decrypt(m_peerSenderKeys.value(userId), sequence, payload,
                                       &decrypted);
        if (!decrypted)
            opus = VoiceCrypto::decrypt(m_key, sequence, payload, &decrypted);
        if (!decrypted && !m_roomKey.isEmpty() && m_roomKey != m_key) {
            // Одна из сторон могла ещё не перейти на парный ключ.
            opus = VoiceCrypto::decrypt(m_roomKey, sequence, payload, &decrypted);
            if (decrypted)
                qInfo("VOICE: пакет расшифрован ключом комнаты (fallback)");
        }
        if (!decrypted) {
            qWarning("VOICE: не удалось расшифровать пакет от пользователя %u", userId);
            continue;
        }

        const quint64 key = (static_cast<quint64>(userId) << 32) | sequence;
        ++m_packetsReceived;
        m_jitter.insert(key, opus);
        while (m_jitter.size() > kMaxJitterPackets)
            m_jitter.erase(m_jitter.begin());
    }
}

void VoiceService::flushJitter()
{
    if (!m_outputDevice || m_jitter.isEmpty())
        return;

    // Небольшой буфер против джиттера: копим 3 пакета, затем играем.
    if (!m_jitterPrimed) {
        if (m_jitter.size() < 3)
            return;
        m_jitterPrimed = true;
    }
    if (m_jitter.size() > 12) {
        while (m_jitter.size() > 6)
            m_jitter.erase(m_jitter.begin());
    }

    if (m_output && m_output->bytesFree() <= 0)
        return;

    const auto it = m_jitter.begin();
    const QByteArray packet = it.value();
    m_jitter.erase(m_jitter.begin());

    const QByteArray pcm = m_codec->decode(packet);
    if (!pcm.isEmpty()) {
        const QByteArray converted = convertPcm(pcm, voiceFormat(), m_outputFormat);
        m_outputBytes += m_outputDevice->write(converted);
    }
}

void VoiceService::onSocketDisconnected()
{
    if (m_state == State::Idle)
        return;

    closeAudio();
    closeUdp();
    m_jitter.clear();
    m_jitterPrimed = false;
    m_chatId = 0;
    m_callerId = 0;
    setState(State::Idle);
}

void VoiceService::onEvent(const QString &name, const QJsonObject &data)
{
    const qint64 chatId = static_cast<qint64>(
        data.value(QStringLiteral("chat_id")).toDouble());

    if (name == QLatin1String(core::proto::event::kVoice)) {
        const bool joined = data.value(QStringLiteral("joined")).toBool(true);
        if (!joined && chatId == m_chatId && m_state != State::Idle) {
            stopCallTimer();
            closeAudio();
            closeUdp();
            m_jitter.clear();
    m_jitterPrimed = false;
            m_chatId = 0;
            m_callerId = 0;
            m_callerName.clear();
            setState(State::Idle);
        }
        return;
    }

    if (name == QLatin1String(core::proto::event::kVoiceSenderKeyEvent)) {
        if (chatId == m_chatId) {
            const qint64 fromUserId = static_cast<qint64>(
                data.value(QStringLiteral("from_user_id")).toDouble());
            const QByteArray blob = QByteArray::fromBase64(
                data.value(QStringLiteral("payload")).toString().toLatin1());
            const QByteArray pairwise = m_pairwiseKeys.value(fromUserId);
            if (!pairwise.isEmpty() && !blob.isEmpty()) {
                bool decrypted = false;
                const QByteArray senderKey =
                    VoiceCrypto::decrypt(pairwise, 0, blob, &decrypted);
                if (decrypted && senderKey.size() == 32)
                    m_peerSenderKeys.insert(fromUserId, senderKey);
            }
        }
        return;
    }

    if (name == QLatin1String(core::proto::event::kVoicePubkeyEvent)) {
        if (chatId == m_chatId) {
            const qint64 userId = static_cast<qint64>(
                data.value(QStringLiteral("user_id")).toDouble());
            applyPeerKey(userId, data.value(QStringLiteral("pubkey")).toString());
        }
        return;
    }

    if (name == QLatin1String(core::proto::event::kVoiceInvite)) {
        const qint64 callerId = static_cast<qint64>(
            data.value(QStringLiteral("caller_id")).toDouble());
        const QString callerName = data.value(QStringLiteral("caller_name")).toString();

        if (m_state != State::Idle) {
            // Занято: вежливо отклоняем с причиной.
            decline(chatId, callerId, QStringLiteral("busy"));
            return;
        }

        m_chatId = chatId;
        m_callerId = callerId;
        m_callerName = callerName;
        setState(State::Ringing);
        startCallTimer();
        emit incomingCall(chatId, callerId, callerName);
        return;
    }

    if (name == QLatin1String(core::proto::event::kVoiceAccepted)) {
        if (m_state != State::Calling)
            return;
        stopCallTimer();
        setState(State::Active);
        qInfo("VOICE: вызов принят, разговор начат");
        emit callAccepted(chatId, static_cast<qint64>(
                                    data.value(QStringLiteral("user_id")).toDouble()));
        return;
    }

    if (name == QLatin1String(core::proto::event::kVoiceDeclined)) {
        if (m_state == State::Calling) {
            stopCallTimer();
            closeAudio();
            closeUdp();
            setState(State::Idle);

            if (data.value(QStringLiteral("reason")).toString() == QLatin1String("busy"))
                emit errorOccurred(tr("Абонент уже в другом звонке."));

            emit callDeclined(chatId, static_cast<qint64>(
                                        data.value(QStringLiteral("user_id")).toDouble()));
        }
        return;
    }

    if (name == QLatin1String(core::proto::event::kVoiceSpeaking)) {
        emit speakingChanged(chatId,
                             static_cast<qint64>(data.value(QStringLiteral("user_id")).toDouble()),
                             data.value(QStringLiteral("on")).toBool(false));
        return;
    }

    if (name == QLatin1String(core::proto::event::kVoiceCancelled)) {
        if (m_state == State::Ringing) {
            stopCallTimer();
            closeAudio();
            closeUdp();
            m_chatId = 0;
            m_callerId = 0;
            m_callerName.clear();
            setState(State::Idle);
            emit callCancelled(chatId);
        }
    }
}

void VoiceService::startCallTimer()
{
    if (m_callTimer)
        m_callTimer->start();
}

void VoiceService::stopCallTimer()
{
    if (m_callTimer)
        m_callTimer->stop();
}

} // namespace app
