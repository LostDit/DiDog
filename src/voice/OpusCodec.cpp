#include "voice/OpusCodec.h"

#include <QLibrary>
#include <QDebug>

#include <opus.h>

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 1;
constexpr int kFrameSamples = 960; // 20 мс
constexpr int kPcmFrameBytes = kFrameSamples * 2;

struct OpusApi {
    QLibrary *library = nullptr;

    OpusEncoder *(*encoder_create)(opus_int32, int, int, int *) = nullptr;
    int (*encoder_ctl)(OpusEncoder *, int, ...) = nullptr;
    opus_int32 (*encode)(OpusEncoder *, const opus_int16 *, int, unsigned char *,
                         opus_int32) = nullptr;
    void (*encoder_destroy)(OpusEncoder *) = nullptr;

    OpusDecoder *(*decoder_create)(opus_int32, int, int *) = nullptr;
    int (*decode)(OpusDecoder *, const unsigned char *, opus_int32, opus_int16 *, int,
                  int) = nullptr;
    void (*decoder_destroy)(OpusDecoder *) = nullptr;
};

OpusApi &api()
{
    static OpusApi instance;
    return instance;
}

bool loadApi(QString *error)
{
    OpusApi &a = api();
    if (a.library)
        return true;

    const QStringList candidates = {
        QStringLiteral("libopus"),
        QStringLiteral("opus"),
#ifdef Q_OS_LINUX
        QStringLiteral("libopus.so.0"),
#endif
    };

    QStringList failures;

    for (const QString &name : candidates) {
        auto *library = new QLibrary(name);
        if (!library->load()) {
            failures.append(QStringLiteral("%1: %2").arg(name, library->errorString()));
            delete library;
            continue;
        }

        a.encoder_create = reinterpret_cast<decltype(a.encoder_create)>(
            library->resolve("opus_encoder_create"));
        a.encoder_ctl = reinterpret_cast<decltype(a.encoder_ctl)>(
            library->resolve("opus_encoder_ctl"));
        a.encode = reinterpret_cast<decltype(a.encode)>(library->resolve("opus_encode"));
        a.encoder_destroy = reinterpret_cast<decltype(a.encoder_destroy)>(
            library->resolve("opus_encoder_destroy"));
        a.decoder_create = reinterpret_cast<decltype(a.decoder_create)>(
            library->resolve("opus_decoder_create"));
        a.decode = reinterpret_cast<decltype(a.decode)>(library->resolve("opus_decode"));
        a.decoder_destroy = reinterpret_cast<decltype(a.decoder_destroy)>(
            library->resolve("opus_decoder_destroy"));

        if (a.encoder_create && a.encoder_ctl && a.encode && a.encoder_destroy
            && a.decoder_create && a.decode && a.decoder_destroy) {
            a.library = library;
            return true;
        }

        delete library;
        a = OpusApi();
    }

    if (error) {
        *error = QStringLiteral("Библиотека Opus не найдена (libopus.dll / libopus.so.0).");
        if (!failures.isEmpty())
            *error += QStringLiteral(" Причина: %1").arg(failures.first());
    }
    return false;
}

} // namespace

OpusCodec::OpusCodec() = default;

OpusCodec::~OpusCodec()
{
    close();
}

bool OpusCodec::isAvailable(QString *error)
{
    return loadApi(error);
}

void OpusCodec::setBitrate(int bitrate)
{
    m_bitrate = qBound(6000, bitrate, 510000);
    if (m_encoder) {
        api().encoder_ctl(reinterpret_cast<OpusEncoder *>(m_encoder),
                          OPUS_SET_BITRATE_REQUEST, m_bitrate);
    }
}

bool OpusCodec::open(QString *error)
{
    if (isOpen())
        return true;

    if (!loadApi(error))
        return false;

    int opusError = 0;
    m_encoder = api().encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &opusError);
    if (!m_encoder || opusError != OPUS_OK) {
        if (error)
            *error = QStringLiteral("Не удалось создать Opus-кодировщик (код %1)").arg(opusError);
        close();
        return false;
    }

    api().encoder_ctl(reinterpret_cast<OpusEncoder *>(m_encoder), OPUS_SET_BITRATE_REQUEST,
                      m_bitrate);
    api().encoder_ctl(reinterpret_cast<OpusEncoder *>(m_encoder), OPUS_SET_INBAND_FEC_REQUEST, 1);
    api().encoder_ctl(reinterpret_cast<OpusEncoder *>(m_encoder),
                      OPUS_SET_PACKET_LOSS_PERC_REQUEST, 10);
    // Максимальное качество голоса: 96 кбит/с, VBR, complexity 10, без DTX.
    api().encoder_ctl(reinterpret_cast<OpusEncoder *>(m_encoder), 4006 /*VBR*/, 1);
    api().encoder_ctl(reinterpret_cast<OpusEncoder *>(m_encoder), 4010 /*COMPLEXITY*/, 10);
    api().encoder_ctl(reinterpret_cast<OpusEncoder *>(m_encoder), 4024 /*SIGNAL*/, 3001 /*VOICE*/);
    api().encoder_ctl(reinterpret_cast<OpusEncoder *>(m_encoder), 4016 /*DTX*/, 0);

    m_decoder = api().decoder_create(kSampleRate, kChannels, &opusError);
    if (!m_decoder || opusError != OPUS_OK) {
        if (error)
            *error = QStringLiteral("Не удалось создать Opus-декодер (код %1)").arg(opusError);
        close();
        return false;
    }

    return true;
}

void OpusCodec::close()
{
    if (m_encoder) {
        api().encoder_destroy(reinterpret_cast<OpusEncoder *>(m_encoder));
        m_encoder = nullptr;
    }
    if (m_decoder) {
        api().decoder_destroy(reinterpret_cast<OpusDecoder *>(m_decoder));
        m_decoder = nullptr;
    }
}

QByteArray OpusCodec::encode(const QByteArray &pcm)
{
    if (!m_encoder || pcm.size() < kPcmFrameBytes)
        return QByteArray();

    unsigned char buffer[4000];
    const auto *samples = reinterpret_cast<const opus_int16 *>(pcm.constData());
    const opus_int32 length = api().encode(reinterpret_cast<OpusEncoder *>(m_encoder), samples,
                                           kFrameSamples, buffer, sizeof(buffer));

    if (length <= 0)
        return QByteArray();

    return QByteArray(reinterpret_cast<const char *>(buffer), static_cast<int>(length));
}

QByteArray OpusCodec::decode(const QByteArray &packet)
{
    if (!m_decoder)
        return QByteArray();

    QByteArray pcm(kPcmFrameBytes, Qt::Uninitialized);
    auto *samples = reinterpret_cast<opus_int16 *>(pcm.data());

    const int decoded = api().decode(
        reinterpret_cast<OpusDecoder *>(m_decoder),
        packet.isEmpty() ? nullptr : reinterpret_cast<const unsigned char *>(packet.constData()),
        packet.size(), samples, kFrameSamples, 0);

    if (decoded <= 0)
        return QByteArray();

    pcm.resize(decoded * 2);
    return pcm;
}
