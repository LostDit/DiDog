#include "voice/AudioProcessor.h"

#include <QtMath>

namespace voice {
namespace {

constexpr float kHighPassPole = 0.995f; // ≈80 Гц при 48 кГц
constexpr float kGateCloseRms = 0.0025f; // ≈ −52 dBFS
constexpr float kGateOpenRms = 0.010f;   // ≈ −40 dBFS
constexpr float kAgcTargetRms = 0.12f;   // ≈ −18 dBFS
constexpr float kAgcMinGain = 0.5f;
constexpr float kAgcMaxGain = 4.0f;

} // namespace

void AudioProcessor::reset()
{
    m_highPassPrevInput = 0.0f;
    m_highPassPrevOutput = 0.0f;
    m_gateGain = 1.0f;
    m_agcGain = 1.0f;
}

void AudioProcessor::process(qint16 *samples, int count)
{
    if (!samples || count <= 0)
        return;

    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        const float value = samples[i] / 32768.0f;
        sum += value * value;
    }
    const float rms = static_cast<float>(qSqrt(sum / count));

    float gateTarget = 1.0f;
    if (m_noiseSuppression) {
        if (rms <= kGateCloseRms)
            gateTarget = 0.0f;
        else if (rms < kGateOpenRms)
            gateTarget = (rms - kGateCloseRms) / (kGateOpenRms - kGateCloseRms);
    }
    // Плавное открытие/закрытие, чтобы не было щелчков.
    m_gateGain += (gateTarget - m_gateGain) * 0.3f;

    if (m_agc && rms > kGateCloseRms) {
        const float desired = qBound(kAgcMinGain, kAgcTargetRms / rms, kAgcMaxGain);
        m_agcGain += (desired - m_agcGain) * 0.08f;
    }

    for (int i = 0; i < count; ++i) {
        float value = samples[i] / 32768.0f;

        // High-pass (DC и низкочастотный гул).
        const float filtered = kHighPassPole * (m_highPassPrevOutput + value - m_highPassPrevInput);
        m_highPassPrevInput = value;
        m_highPassPrevOutput = filtered;
        value = filtered;

        value *= m_gateGain * m_agcGain;
        samples[i] = static_cast<qint16>(qBound(-1.0f, value, 1.0f) * 32767.0f);
    }
}

} // namespace voice
