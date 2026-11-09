#include "voice/VoiceCrypto.h"

#include <QByteArray>
#include <QtTest>

// AES-256-GCM для голосовых пакетов: шифрование, расшифровка и защита от подмены.
class TestVoiceCrypto : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip();
    void tamperingIsRejected();
    void replayWithOtherSequenceFails();
    void e2eeKeyExchangeMatches();
    void hkdfIsDeterministic();
};

void TestVoiceCrypto::e2eeKeyExchangeMatches()
{
    QString error;
    if (!VoiceCrypto::available(&error))
        QSKIP(qPrintable(error));

    QByteArray alicePrivate;
    QByteArray alicePublic;
    QByteArray bobPrivate;
    QByteArray bobPublic;
    QVERIFY2(VoiceCrypto::generateKeyPair(&alicePrivate, &alicePublic, &error), qPrintable(error));
    QVERIFY(VoiceCrypto::generateKeyPair(&bobPrivate, &bobPublic, &error));
    QCOMPARE(alicePrivate.size(), 32);
    QCOMPARE(alicePublic.size(), 32);

    bool ok = false;
    const QByteArray aliceSecret =
        VoiceCrypto::deriveSharedSecret(alicePrivate, bobPublic, &ok);
    QVERIFY(ok);
    const QByteArray bobSecret =
        VoiceCrypto::deriveSharedSecret(bobPrivate, alicePublic, &ok);
    QVERIFY(ok);
    QCOMPARE(aliceSecret, bobSecret);
    QCOMPARE(aliceSecret.size(), 32);

    const QByteArray aliceKey = VoiceCrypto::hkdfSha256(
        aliceSecret, "didog-voice", "chat-42");
    const QByteArray bobKey = VoiceCrypto::hkdfSha256(
        bobSecret, "didog-voice", "chat-42");
    QCOMPARE(aliceKey, bobKey);
    QCOMPARE(aliceKey.size(), 32);
}

void TestVoiceCrypto::hkdfIsDeterministic()
{
    const QByteArray secret(32, 'k');
    QCOMPARE(VoiceCrypto::hkdfSha256(secret, "salt", "info"),
             VoiceCrypto::hkdfSha256(secret, "salt", "info"));
    QVERIFY(VoiceCrypto::hkdfSha256(secret, "salt", "info")
            != VoiceCrypto::hkdfSha256(secret, "salt", "other"));
}

void TestVoiceCrypto::roundTrip()
{
    QString error;
    if (!VoiceCrypto::available(&error))
        QSKIP(qPrintable(error));

    QByteArray key(32, 0);
    for (int i = 0; i < key.size(); ++i)
        key[i] = static_cast<char>(i * 7 + 1);

    const QByteArray plain = QByteArray::fromHex("f8ff0102030405060708090a");
    bool ok = false;
    const QByteArray packet = VoiceCrypto::encrypt(key, 42, plain, &ok);
    QVERIFY(ok);
    QVERIFY(packet.size() >= plain.size() + 16);

    const QByteArray restored = VoiceCrypto::decrypt(key, 42, packet, &ok);
    QVERIFY(ok);
    QCOMPARE(restored, plain);
}

void TestVoiceCrypto::tamperingIsRejected()
{
    QString error;
    if (!VoiceCrypto::available(&error))
        QSKIP(qPrintable(error));

    QByteArray key(32, 0x5a);
    const QByteArray plain = QByteArray::fromHex("0102030405060708");

    bool ok = false;
    QByteArray packet = VoiceCrypto::encrypt(key, 7, plain, &ok);
    QVERIFY(ok);

    packet[packet.size() - 1] = static_cast<char>(packet.at(packet.size() - 1) ^ 0x01);
    const QByteArray restored = VoiceCrypto::decrypt(key, 7, packet, &ok);
    QVERIFY(!ok);
    QVERIFY(restored.isEmpty());
}

void TestVoiceCrypto::replayWithOtherSequenceFails()
{
    QString error;
    if (!VoiceCrypto::available(&error))
        QSKIP(qPrintable(error));

    QByteArray key(32, 0x11);
    const QByteArray plain = QByteArray::fromHex("aabbccdd");

    bool ok = false;
    const QByteArray packet = VoiceCrypto::encrypt(key, 100, plain, &ok);
    QVERIFY(ok);

    // Другой seq — другой nonce, расшифровка не должна пройти.
    const QByteArray restored = VoiceCrypto::decrypt(key, 101, packet, &ok);
    QVERIFY(!ok);
    QVERIFY(restored.isEmpty());
}

QTEST_GUILESS_MAIN(TestVoiceCrypto)
#include "tst_voice_crypto.moc"
