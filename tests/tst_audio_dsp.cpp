#include "voice/AudioProcessor.h"

#include <QtMath>
#include <QtTest>

// DSP перед Opus: тишина, шумовой гейт и AGC.
class TestAudioDsp : public QObject
{
    Q_OBJECT

private slots:
    void silenceStaysSilent();
    void gateAttenuatesQuietNoise();
    void agcRaisesQuietSpeech();
};

namespace {

QVector<qint16> tone(int samples, double amplitude, double frequency = 220.0)
{
    QVector<qint16> result(samples);
    for (int i = 0; i < samples; ++i) {
        const double value = amplitude * qSin(2.0 * M_PI * frequency * i / 48000.0);
        result[i] = static_cast<qint16>(value * 32767.0);
    }
    return result;
}

double rms(const QVector<qint16> &samples)
{
    double sum = 0.0;
    for (qint16 sample : samples)
        sum += double(sample) * sample;
    return qSqrt(sum / qMax(1, samples.size())) / 32768.0;
}

} // namespace

void TestAudioDsp::silenceStaysSilent()
{
    voice::AudioProcessor processor;
    QVector<qint16> frame(960, 0);
    processor.process(frame.data(), frame.size());
    QCOMPARE(frame, QVector<qint16>(960, 0));
}

void TestAudioDsp::gateAttenuatesQuietNoise()
{
    voice::AudioProcessor processor;
    QVector<qint16> frame = tone(960, 0.0015);
    const double before = rms(frame);

    for (int i = 0; i < 20; ++i)
        processor.process(frame.data(), frame.size());

    QVERIFY2(rms(frame) < before * 0.5, "тихий шум должен подавляться гейтом");
}

void TestAudioDsp::agcRaisesQuietSpeech()
{
    voice::AudioProcessor processor;
    processor.setNoiseSuppression(false);
    processor.setAgc(true);

    QVector<qint16> frame = tone(960, 0.02);
    const double before = rms(frame);

    for (int i = 0; i < 120; ++i)
        processor.process(frame.data(), frame.size());

    const double after = rms(frame);
    QVERIFY2(after > before, "AGC должен поднять тихую речь");
    QVERIFY2(after < 0.5, "AGC не должен уходить в клиппинг");
}

QTEST_MAIN(TestAudioDsp)
#include "tst_audio_dsp.moc"
