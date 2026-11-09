#include "voice/VoiceCrypto.h"

#include <QMessageAuthenticationCode>
#include <QDebug>
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

    void *(*pkeyCtxNewId)(int, void *) = nullptr;
    void *(*pkeyCtxNew)(void *, void *) = nullptr;
    void (*pkeyCtxFree)(void *) = nullptr;
    int (*keygenInit)(void *) = nullptr;
    int (*keygen)(void *, void **) = nullptr;
    void (*pkeyFree)(void *) = nullptr;
    int (*getRawPublic)(const void *, unsigned char *, size_t *) = nullptr;
    int (*getRawPrivate)(const void *, unsigned char *, size_t *) = nullptr;
    void *(*newRawPrivate)(int, void *, const unsigned char *, size_t) = nullptr;
    void *(*newRawPublic)(int, void *, const unsigned char *, size_t) = nullptr;
    int (*deriveInit)(void *) = nullptr;
    int (*deriveSetPeer)(void *, void *) = nullptr;
    int (*derive)(void *, unsigned char *, size_t *) = nullptr;
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
        a.pkeyCtxNewId = reinterpret_cast<decltype(a.pkeyCtxNewId)>(library->resolve("EVP_PKEY_CTX_new_id"));
        a.pkeyCtxNew = reinterpret_cast<decltype(a.pkeyCtxNew)>(library->resolve("EVP_PKEY_CTX_new"));
        a.pkeyCtxFree = reinterpret_cast<decltype(a.pkeyCtxFree)>(library->resolve("EVP_PKEY_CTX_free"));
        a.keygenInit = reinterpret_cast<decltype(a.keygenInit)>(library->resolve("EVP_PKEY_keygen_init"));
        a.keygen = reinterpret_cast<decltype(a.keygen)>(library->resolve("EVP_PKEY_keygen"));
        a.pkeyFree = reinterpret_cast<decltype(a.pkeyFree)>(library->resolve("EVP_PKEY_free"));
        a.getRawPublic = reinterpret_cast<decltype(a.getRawPublic)>(library->resolve("EVP_PKEY_get_raw_public_key"));
        a.getRawPrivate = reinterpret_cast<decltype(a.getRawPrivate)>(library->resolve("EVP_PKEY_get_raw_private_key"));
        a.newRawPrivate = reinterpret_cast<decltype(a.newRawPrivate)>(library->resolve("EVP_PKEY_new_raw_private_key"));
        a.newRawPublic = reinterpret_cast<decltype(a.newRawPublic)>(library->resolve("EVP_PKEY_new_raw_public_key"));
        a.deriveInit = reinterpret_cast<decltype(a.deriveInit)>(library->resolve("EVP_PKEY_derive_init"));
        a.deriveSetPeer = reinterpret_cast<decltype(a.deriveSetPeer)>(library->resolve("EVP_PKEY_derive_set_peer"));
        a.derive = reinterpret_cast<decltype(a.derive)>(library->resolve("EVP_PKEY_derive"));

        if (a.ctxNew && a.ctxFree && a.aes256gcm && a.encryptInit && a.encryptUpdate
            && a.encryptFinal && a.decryptInit && a.decryptUpdate && a.decryptFinal && a.ctrl
            && a.pkeyCtxNewId && a.pkeyCtxNew && a.pkeyCtxFree && a.keygenInit && a.keygen && a.pkeyFree
            && a.getRawPublic && a.getRawPrivate && a.newRawPrivate && a.newRawPublic && a.deriveInit
            && a.deriveSetPeer && a.derive) {
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

namespace {
constexpr int kEvpPkeyX25519 = 1034;
}

bool VoiceCrypto::generateKeyPair(QByteArray *privateKey, QByteArray *publicKey,
                                  QString *error)
{
    if (!loadApi(error))
        return false;

    void *ctx = api().pkeyCtxNewId(kEvpPkeyX25519, nullptr);
    if (!ctx) {
        if (error)
            *error = QStringLiteral("Не удалось создать контекст X25519.");
        return false;
    }

    void *key = nullptr;
    bool ok = api().keygenInit(ctx) == 1 && api().keygen(ctx, &key) == 1 && key;

    if (ok) {
        unsigned char raw[32];
        size_t length = sizeof(raw);
        ok = api().getRawPrivate(key, raw, &length) == 1 && length == 32;
        if (ok && privateKey)
            *privateKey = QByteArray(reinterpret_cast<char *>(raw), 32);

        length = sizeof(raw);
        ok = ok && api().getRawPublic(key, raw, &length) == 1 && length == 32;
        if (ok && publicKey)
            *publicKey = QByteArray(reinterpret_cast<char *>(raw), 32);
    }

    if (key)
        api().pkeyFree(key);
    api().pkeyCtxFree(ctx);

    if (!ok && error)
        *error = QStringLiteral("Не удалось сгенерировать ключевую пару X25519.");
    return ok;
}

QByteArray VoiceCrypto::deriveSharedSecret(const QByteArray &privateKey,
                                           const QByteArray &peerPublic, bool *ok)
{
    if (ok)
        *ok = false;
    if (privateKey.size() != 32 || peerPublic.size() != 32)
        return QByteArray();
    if (!loadApi(nullptr))
        return QByteArray();

    void *privatePkey = api().newRawPrivate(
        kEvpPkeyX25519, nullptr,
        reinterpret_cast<const unsigned char *>(privateKey.constData()), 32);
    void *peerPkey = api().newRawPublic(
        kEvpPkeyX25519, nullptr,
        reinterpret_cast<const unsigned char *>(peerPublic.constData()), 32);
    if (!privatePkey || !peerPkey) {
        if (privatePkey)
            api().pkeyFree(privatePkey);
        if (peerPkey)
            api().pkeyFree(peerPkey);
        return QByteArray();
    }

    void *ctx = api().pkeyCtxNew(privatePkey, nullptr);
    QByteArray secret;
    const int initResult = ctx ? api().deriveInit(ctx) : -1;
    const int peerResult = initResult > 0 ? api().deriveSetPeer(ctx, peerPkey) : -1;
    bool success = initResult > 0 && peerResult > 0;
    if (!success) {
        qWarning("x25519 derive init=%d peer=%d ctx=%p priv=%p peer=%p", initResult,
                 peerResult, ctx, privatePkey, peerPkey);
    }

    if (success) {
        unsigned char buffer[32];
        size_t length = sizeof(buffer);
        success = api().derive(ctx, buffer, &length) == 1 && length == 32;
        if (success)
            secret = QByteArray(reinterpret_cast<char *>(buffer), 32);
    }

    if (ctx)
        api().pkeyCtxFree(ctx);
    api().pkeyFree(privatePkey);
    api().pkeyFree(peerPkey);

    if (ok)
        *ok = success;
    return secret;
}

QByteArray VoiceCrypto::hkdfSha256(const QByteArray &secret, const QByteArray &salt,
                                   const QByteArray &info, int length)
{
    const QByteArray effectiveSalt = salt.isEmpty() ? QByteArray(32, '\0') : salt;
    const QByteArray prk = QMessageAuthenticationCode::hash(
        secret, effectiveSalt, QCryptographicHash::Sha256);

    QByteArray output;
    QByteArray block;
    quint8 counter = 1;
    while (output.size() < length) {
        QMessageAuthenticationCode mac(QCryptographicHash::Sha256, prk);
        mac.addData(block);
        mac.addData(info);
        mac.addData(reinterpret_cast<const char *>(&counter), 1);
        block = mac.result();
        output.append(block);
        ++counter;
    }
    return output.left(length);
}