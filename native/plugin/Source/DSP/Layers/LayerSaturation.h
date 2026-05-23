#pragma once
//==============================================================================
//  LayerSaturation.h — Post-layer "glue" saturation.
//
//  Subtle analog-style tanh waveshaping with a parallel dry/wet mix. Sits on
//  the shared layer bus AFTER all layers are summed so the combined signal
//  develops common harmonics — that is what makes a stack of samples stop
//  sounding like "samples stacked" and start sounding like one instrument.
//
//  Tuned for low-cost realtime use: one tanh + one mul per sample per channel.
//==============================================================================
#include <cmath>
#include <algorithm>

class LayerSaturation
{
public:
    void setDrive(float d) noexcept { drive = std::clamp(d, 0.0f, 1.0f); }
    void setMix  (float m) noexcept { mix   = std::clamp(m, 0.0f, 1.0f); }

    inline float process(float x) const noexcept
    {
        // 1 + 0..3.5x pre-gain keeps things gentle in the recommended 5-18% drive.
        const float pre = x * (1.0f + drive * 3.5f);
        const float wet = std::tanh(pre) * (1.0f / (1.0f + drive * 1.8f));
        return x * (1.0f - mix) + wet * mix;
    }

private:
    float drive = 0.12f;   // ~12% default
    float mix   = 0.22f;   // ~22% parallel blend
};
