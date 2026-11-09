#pragma once

#include <QtGlobal>

namespace voice {

// Простая обработка речи перед Opus: high-pass, шумовой гейт и AGC.
// Работает с моно PCM 16 бит (48 кГц), кадр 20 мс.
class AudioProcessor
{
public:
    void setNoiseSuppression(bool enabled) { m_noiseSuppression = enabled; }
    void setAgc(bool enabled) { m_agc = enabled; }
    void reset();

    void process(qint16 *samples, int count);

private:
    bool m_noiseSuppression = true;
    bool m_agc = true;

    float m_highPassPrevInput = 0.0f;
    float m_highPassPrevOutput = 0.0f;
    float m_gateGain = 1.0f;
    float m_agcGain = 1.0f;
};

} // namespace voice
