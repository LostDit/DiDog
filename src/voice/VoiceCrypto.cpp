#include "voice/VoiceCrypto.h"

#include <QLibrary>
#include <QtEndian>

namespace {

constexpr int kKeyBytes = 32;
constexpr int kNonceBytes = 12;
constexpr int kTagBytes = 16;

// Значения из openssl/evp.h (EVP_CTRL_GCM_GET_TAG/SET_TAG).
constexpr int kGetTag = 0x10;
constexpr int kSetTag = 0x11;

struct EvpApi {
    QLibrary *library = nullptr;

    void *(*ctxNew)() = nullptr;
    void (*ctxFree)(void *) = nullptr;
    const void *(*aes256gcm)() = nullptr;
    int (*encryptInit)(void *, const void *, void *, const unsigned char *,
                       const unsigned char *) = nullptr;
    int (*encryptUpdate)(void *, unsigned char *, int *, const unsigned char *, int) = nullptr;
    int (*encryptFinal)(void *, unsigned char *, int *) = nullptr;
    int (*decryptInit)(void *, const void *, void *, const unsigned char *,
                       const unsigned char *) = nullptr;
    int (*decryptUpdate)(void *, unsigned char *, int *, const unsigned char *, int) = nullptr;
    int (*decryptFinal)(void *, unsigned char *, int *) = nullptr;
    int (*ctrl)(void *, int, int, void *) = nullptr;
};

EvpApi &api()
{
    static EvpApi instance;
    return instance;
}

bool loadApi(QString *error)
{
    EvpApi &a = api();
    if (a.library)
        return true;

    const QStringList candidates = {
        QStringLiteral("libcrypto-1_1-x64"),
        QStringLiteral("libcrypto"),
        QStringLiteral("libcrypto.so.3"),
        QStringLiteral("libcrypto.so.1.1"),
    };

    QStringList failures;
    for (const QString &name : candidates) {
        auto *library = new QLibrary(name);
        if (!library->load()) {
            failures.append(QStringLiteral("%1: %2").arg(name, library->errorString()));
            delete library;
            continue;
        }

        a.ctxNew = reinterpret_cast<decltype(a.ctxNew)>(library->resolve("EVP_CIPHER_CTX_new"));
        a.ctxFree = reinterpret_cast<decltype(a.ctxFree)>(library->resolve("EVP_CIPHER_CTX_free"));
        a.aes256gcm = reinterpret_cast<decltype(a.aes256gcm)>(
            library->resolve("EVP_aes_256_gcm"));
        a.encryptInit = reinterpret_cast<decltype(a.encryptInit)>(
            library->resolve("EVP_EncryptInit_ex"));
        a.encryptUpdate = reinterpret_cast<decltype(a.encryptUpdate)>(
            library->resolve("EVP_EncryptUpdate"));
        a.encryptFinal = reinterpret_cast<decltype(a.encryptFinal)>(
            library->resolve("EVP_EncryptFinal_ex"));
        a.decryptInit = reinterpret_cast<decltype(a.decryptInit)>(
            library->resolve("EVP_DecryptInit_ex"));
        a.decryptUpdate = reinterpret_cast<decltype(a.decryptUpdate)>(
            library->resolve("EVP_DecryptUpdate"));
        a.decryptFinal = reinterpret_cast<decltype(a.decryptFinal)>(
            library->resolve("EVP_DecryptFinal_ex"));
        a.ctrl = reinterpret_cast<decltype(a.ctrl)>(library->resolve("EVP_CIPHER_CTX_ctrl"));

        if (a.ctxNew && a.ctxFree && a.aes256gcm && a.encryptInit && a.encryptUpdate
            && a.encryptFinal && a.decryptInit && a.decryptUpdate && a.decryptFinal && a.ctrl) {
            a.library = library;
            return true;
        }

        delete library;
        a = EvpApi();
    }

    if (error) {
        *error = QStringLiteral("Не удалось загрузить OpenSSL для шифрования голоса.");
        if (!failures.isEmpty())
            *error += QStringLiteral(" Причина: %1").arg(failures.first());
    }
    return false;
}

} // namespace

bool VoiceCrypto::available(QString *error)
{
    return loadApi(error);
}

QByteArray VoiceCrypto::nonceFor(quint32 sequence)
{
    QByteArray nonce(kNonceBytes, 0);
    qToBigEndian<quint32>(sequence, nonce.data() + kNonceBytes - 4);
    return nonce;
}

QByteArray VoiceCrypto::encrypt(const QByteArray &key, quint32 sequence,
                                const QByteArray &plain, bool *ok)
{
    if (ok)
        *ok = false;
    if (key.size() != kKeyBytes || plain.isEmpty())
        return QByteArray();
    if (!loadApi(nullptr))
        return QByteArray();

    const QByteArray nonce = nonceFor(sequence);

    void *ctx = api().ctxNew();
    if (!ctx)
        return QByteArray();

    QByteArray out(plain.size() + kTagBytes, Qt::Uninitialized);
    int length = 0;
    int total = 0;

    bool success = api().encryptInit(ctx, api().aes256gcm(), nullptr,
                                     reinterpret_cast<const unsigned char *>(key.constData()),
                                     reinterpret_cast<const unsigned char *>(nonce.constData()))
                   == 1;

    if (success) {
        success = api().encryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &length,
                                      reinterpret_cast<const unsigned char *>(plain.constData()),
                                      plain.size())
                  == 1;
        total = length;
    }

    if (success) {
        success = api().encryptFinal(ctx, reinterpret_cast<unsigned char *>(out.data()) + total,
                                     &length)
                  == 1;
        total += length;
    }

    if (success) {
        success = api().ctrl(ctx, kGetTag, kTagBytes,
                             reinterpret_cast<void *>(out.data() + total))
                  == 1;
    }

    api().ctxFree(ctx);

    if (!success)
        return QByteArray();

    out.resize(total + kTagBytes);
    if (ok)
        *ok = true;
    return out;
}

QByteArray VoiceCrypto::decrypt(const QByteArray &key, quint32 sequence,
                                const QByteArray &packet, bool *ok)
{
    if (ok)
        *ok = false;
    if (key.size() != kKeyBytes || packet.size() <= kTagBytes)
        return QByteArray();
    if (!loadApi(nullptr))
        return QByteArray();

    const QByteArray nonce = nonceFor(sequence);
    const int cipherSize = packet.size() - kTagBytes;

    void *ctx = api().ctxNew();
    if (!ctx)
        return QByteArray();

    QByteArray out(cipherSize, Qt::Uninitialized);
    int length = 0;
    int total = 0;

    bool success = api().decryptInit(ctx, api().aes256gcm(), nullptr,
                                     reinterpret_cast<const unsigned char *>(key.constData()),
                                     reinterpret_cast<const unsigned char *>(nonce.constData()))
                   == 1;

    if (success) {
        success = api().decryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &length,
                                      reinterpret_cast<const unsigned char *>(packet.constData()),
                                      cipherSize)
                  == 1;
        total = length;
    }

    if (success) {
        success = api().ctrl(ctx, kSetTag, kTagBytes,
                             const_cast<char *>(packet.constData() + cipherSize))
                  == 1;
    }

    if (success) {
        success = api().decryptFinal(ctx, reinterpret_cast<unsigned char *>(out.data()) + total,
                                     &length)
                  == 1;
    }

    api().ctxFree(ctx);

    if (!success)
        return QByteArray();

    out.resize(total);
    if (ok)
        *ok = true;
    return out;
}
