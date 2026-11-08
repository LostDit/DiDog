#include "voice/VoiceService.h"

#include "core/Protocol.h"
#include "net/ClientConnection.h"
#include "voice/OpusCodec.h"

#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QAudioInput>
#include <QAudioOutput>
#include <QDateTime>
#include <QDebug>
#include <QHostInfo>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

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

VoiceService::VoiceService(net::ClientConnection *connection, QObject *parent)
    : QObject(parent)
    , m_connection(connection)
    , m_codec(new OpusCodec)
    , m_keepalive(new QTimer(this))
    , m_jitterTimer(new QTimer(this))
{
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

    QString codecError;
    if (!m_codec->open(&codecError)) {
        emit errorOccurred(codecError);
        return;
    }

    m_chatId = chatId;
    m_callerId = 0;
    setState(State::Calling);

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
            openUdp(static_cast<quint16>(data.value(QStringLiteral("udp_port")).toInt()),
                    static_cast<quint32>(data.value(QStringLiteral("udp_token")).toDouble()));
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

    QString codecError;
    if (!m_codec->open(&codecError)) {
        emit errorOccurred(codecError);
        return;
    }

    m_chatId = chatId;
    m_callerId = callerId;
    setState(State::Active);

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
            openUdp(static_cast<quint16>(data.value(QStringLiteral("udp_port")).toInt()),
                    static_cast<quint32>(data.value(QStringLiteral("udp_token")).toDouble()));
            openAudio();

            m_connection->request(
                QStringLiteral("voice.accept"),
                QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(chatId)},
                            {QStringLiteral("caller_id"), static_cast<double>(callerId)}},
                nullptr, 5000);
        },
        10000);
}

void VoiceService::decline(qint64 chatId, qint64 callerId)
{
    if (m_connection && m_connection->isConnected()) {
        m_connection->request(
            QStringLiteral("voice.decline"),
            QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(chatId)},
                        {QStringLiteral("caller_id"), static_cast<double>(callerId)}},
            nullptr, 5000);
    }

    if (m_state == State::Ringing)
        setState(State::Idle);
}

void VoiceService::cancel(qint64 chatId)
{
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

    if (m_connection && m_connection->isConnected()) {
        m_connection->request(
            QStringLiteral("voice.leave"),
            QJsonObject{{QStringLiteral("chat_id"), static_cast<double>(m_chatId)}},
            nullptr, 2000);
    }

    closeAudio();
    closeUdp();
    m_jitter.clear();
    m_expectedSequence = 0;
    m_chatId = 0;
    m_callerId = 0;
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
    m_jitterTimer->start();
}

void VoiceService::closeAudio()
{
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

void VoiceService::openUdp(quint16 port, quint32 token)
{
    closeUdp();

    m_serverVoicePort = port;
    m_token = token;

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

    m_expectedSequence = 0;
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
}

void VoiceService::sendDatagram(const QByteArray &payload)
{
    if (!m_udp || m_token == 0 || m_serverAddress.isNull() || m_serverVoicePort == 0)
        return;

    const QByteArray datagram =
        core::proto::encodeVoiceDatagram(m_token, m_sendSequence++, payload);
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
    if (!m_inputDevice || m_state != State::Active)
        return;

    m_captureBuffer.append(m_inputDevice->readAll());

    while (m_captureBuffer.size() >= kFrameBytes) {
        const QByteArray frame = m_captureBuffer.left(kFrameBytes);
        m_captureBuffer.remove(0, kFrameBytes);

        updateSpeaking(frame);

        if (m_muted)
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

        if (m_expectedSequence == 0)
            m_expectedSequence = sequence;

        if (sequence < m_expectedSequence)
            continue;

        m_jitter.insert(sequence, payload);
        while (m_jitter.size() > kMaxJitterPackets)
            m_jitter.erase(m_jitter.begin());
    }
}

void VoiceService::flushJitter()
{
    if (!m_outputDevice || m_jitter.isEmpty())
        return;

    if (m_output && m_output->bytesFree() <= 0)
        return;

    const auto it = m_jitter.begin();
    const QByteArray packet = it.value();
    m_expectedSequence = it.key() + 1;
    m_jitter.erase(m_jitter.begin());

    const QByteArray pcm = m_codec->decode(packet);
    if (!pcm.isEmpty())
        m_outputDevice->write(pcm);
}

void VoiceService::onSocketDisconnected()
{
    if (m_state == State::Idle)
        return;

    closeAudio();
    closeUdp();
    m_jitter.clear();
    m_chatId = 0;
    m_callerId = 0;
    setState(State::Idle);
}

void VoiceService::onEvent(const QString &name, const QJsonObject &data)
{
    const qint64 chatId = static_cast<qint64>(
        data.value(QStringLiteral("chat_id")).toDouble());

    if (name == QLatin1String(core::proto::event::kVoiceInvite)) {
        const qint64 callerId = static_cast<qint64>(
            data.value(QStringLiteral("caller_id")).toDouble());
        const QString callerName = data.value(QStringLiteral("caller_name")).toString();

        if (m_state != State::Idle) {
            // Занято: вежливо отклоняем.
            decline(chatId, callerId);
            return;
        }

        m_chatId = chatId;
        m_callerId = callerId;
        setState(State::Ringing);
        emit incomingCall(chatId, callerId, callerName);
        return;
    }

    if (name == QLatin1String(core::proto::event::kVoiceAccepted)) {
        if (m_state != State::Calling)
            return;
        setState(State::Active);
        emit callAccepted(chatId, static_cast<qint64>(
                                    data.value(QStringLiteral("user_id")).toDouble()));
        return;
    }

    if (name == QLatin1String(core::proto::event::kVoiceDeclined)) {
        if (m_state == State::Calling) {
            closeAudio();
            closeUdp();
            setState(State::Idle);
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
            m_chatId = 0;
            m_callerId = 0;
            setState(State::Idle);
            emit callCancelled(chatId);
        }
    }
}

} // namespace app
