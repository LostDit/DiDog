#pragma once

#include <QByteArray>
#include <QString>

// Обёртка над Opus: библиотека загружается динамически (QLibrary),
// поэтому сборка не зависит от наличия opus при компиляции.
class OpusCodec
{
public:
    OpusCodec();
    ~OpusCodec();

    OpusCodec(const OpusCodec &) = delete;
    OpusCodec &operator=(const OpusCodec &) = delete;

    static bool isAvailable(QString *error = nullptr);

    bool open(QString *error = nullptr);
    void setBitrate(int bitrate);
    void close();

    // PCM 48 кГц, моно, 16 бит, 20 мс = 1920 байт.
    QByteArray encode(const QByteArray &pcm);
    QByteArray decode(const QByteArray &packet, bool fec = false);

    bool isOpen() const { return m_encoder != nullptr && m_decoder != nullptr; }

private:
    void *m_encoder = nullptr;
    void *m_decoder = nullptr;
    int m_bitrate = 96000;
};
