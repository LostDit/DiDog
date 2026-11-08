#pragma once

#include <QByteArray>
#include <QString>

// AES-256-GCM для голосовых пакетов. OpenSSL загружается динамически
// (libcrypto уже поставляется вместе с приложением для TLS).
class VoiceCrypto
{
public:
    static bool available(QString *error = nullptr);

    // nonce (12 байт) + ciphertext + tag (16 байт) в одном массиве.
    static QByteArray encrypt(const QByteArray &key, quint32 sequence,
                              const QByteArray &plain, bool *ok = nullptr);
    static QByteArray decrypt(const QByteArray &key, quint32 sequence,
                              const QByteArray &packet, bool *ok = nullptr);

    static QByteArray nonceFor(quint32 sequence);
};
