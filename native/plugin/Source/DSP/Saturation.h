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

    void prepare(double sampleRate) noexcept
    {
        const double sr = sampleRate > 0 ? sampleRate : 44100.0;
        // ~10 ms smoothing — eliminates zipper noise when drive/mix automation
        // sweeps. Cheap one-pole, called per-sample inside processSample.
        smoothedDrive.reset(sr, 0.01);
        smoothedMix.reset  (sr, 0.01);
        smoothedDrive.setCurrentAndTargetValue(drive);
        smoothedMix  .setCurrentAndTargetValue(mix);
    }
    void setMode(Mode m)    noexcept { mode = m; }
    void setDrive(float d)  noexcept { drive = std::clamp(d, 0.0f, 1.0f); smoothedDrive.setTargetValue(drive); }
    void setMix(float m)    noexcept { mix   = std::clamp(m, 0.0f, 1.0f); smoothedMix.setTargetValue(mix); }

    float processSample(float x) noexcept
    {
        const float d = smoothedDrive.getNextValue();
        const float m = smoothedMix.getNextValue();
        const float pre = x * (1.0f + d * 9.0f);   // 0..10x
        float wet = 0.0f;
        switch (mode)
        {
            case Mode::Soft:     wet = std::tanh(pre); break;
            case Mode::Tape:     wet = pre / std::sqrt(1.0f + pre * pre); break;     // soft, asymmetric-ish
            case Mode::Tube:     wet = std::tanh(pre + 0.05f * pre * pre); break;    // adds mild even harmonics
            case Mode::HardClip: wet = std::clamp(pre, -1.0f, 1.0f); break;
        }
        // Compensate for drive boost so output stays musically usable.
        const float comp = 1.0f / (1.0f + d * 4.0f);
        wet *= comp;
        return dida::denormalGuard(x * (1.0f - m) + wet * m);
    }

private:
    Mode  mode  = Mode::Soft;
    float drive = 0.0f;
    float mix   = 1.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDrive { 0.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMix   { 1.0f };
};
