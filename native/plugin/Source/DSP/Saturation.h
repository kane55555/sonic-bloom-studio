#pragma once
//==============================================================================
//  Saturation.h — Multi-mode soft saturator / distortion.
//
//  Modes are intentionally cheap so they can run per-sample without spikes.
//  All modes guarantee bounded output (|y| <= 1) so the limiter at the end
//  of the FX chain never has to deal with runaway values.
//==============================================================================
#include <cmath>
#include "UtilityDSP.h"

class Saturation
{
public:
    enum class Mode { Soft, Tape, Tube, HardClip };

    void prepare(double /*sampleRate*/) noexcept {}
    void setMode(Mode m)    noexcept { mode = m; }
    void setDrive(float d)  noexcept { drive = std::clamp(d, 0.0f, 1.0f); }
    void setMix(float m)    noexcept { mix = std::clamp(m, 0.0f, 1.0f); }

    float processSample(float x) noexcept
    {
        const float pre = x * (1.0f + drive * 9.0f);   // 0..10x
        float wet = 0.0f;
        switch (mode)
        {
            case Mode::Soft:     wet = std::tanh(pre); break;
            case Mode::Tape:     wet = pre / std::sqrt(1.0f + pre * pre); break;     // soft, asymmetric-ish
            case Mode::Tube:     wet = std::tanh(pre + 0.05f * pre * pre); break;    // adds mild even harmonics
            case Mode::HardClip: wet = std::clamp(pre, -1.0f, 1.0f); break;
        }
        // Compensate for drive boost so output stays musically usable.
        const float comp = 1.0f / (1.0f + drive * 4.0f);
        wet *= comp;
        return dida::denormalGuard(x * (1.0f - mix) + wet * mix);
    }

private:
    Mode  mode  = Mode::Soft;
    float drive = 0.0f;
    float mix   = 1.0f;
};
