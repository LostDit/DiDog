#include "voice/OpusCodec.h"

#include <QtTest>

// Проверка кодека: если Opus-библиотека недоступна, тест пропускается.
class TestOpus : public QObject
{
    Q_OBJECT

private slots:
    void encodesAndDecodesFrame();
};

void TestOpus::encodesAndDecodesFrame()
{
    QString error;
    if (!OpusCodec::isAvailable(&error))
        QSKIP(qPrintable(error));

    OpusCodec codec;
    QVERIFY2(codec.open(&error), qPrintable(error));

    // 20 мс синусоиды 440 Гц, 48 кГц моно, 16 бит.
    QByteArray pcm(960 * 2, Qt::Uninitialized);
    auto *samples = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < 960; ++i) {
        const double value = std::sin(2.0 * 3.14159265358979 * 440.0 * i / 48000.0);
        samples[i] = static_cast<qint16>(value * 12000);
    }

    const QByteArray packet = codec.encode(pcm);
    QVERIFY(!packet.isEmpty());
    QVERIFY(packet.size() < pcm.size());

    const QByteArray decoded = codec.decode(packet);
    QVERIFY(!decoded.isEmpty());
    QCOMPARE(decoded.size(), 960 * 2);

    // Потеря пакета: декодер должен вернуть кадр (PLC), а не пустоту.
    const QByteArray conceal = codec.decode(QByteArray());
    QCOMPARE(conceal.size(), 960 * 2);
}

QTEST_GUILESS_MAIN(TestOpus)
#include "tst_opus.moc"
