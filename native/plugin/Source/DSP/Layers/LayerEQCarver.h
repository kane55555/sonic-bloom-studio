#pragma once
//==============================================================================
//  LayerEQCarver.h — Tiny one-pole high/low-shelf carvers used per-layer
//  inside SynthVoice to keep stacked layers in their own spectral lane.
//
//  Each instance is one biquad-free TPT one-pole; cheap enough to run per
//  voice per layer. Used to:
//    - high-pass noise/air layers around 2 kHz
//    - low-pass sub/body around 250-400 Hz
//    - high-shelf for "shimmer" presence
//==============================================================================
#include <cmath>
#include <algorithm>

class OnePoleCarver
{
public:
    enum class Mode { LowPass, HighPass };

    void prepare(double sr) noexcept { sampleRate = sr; updateCoef(); }
    void setMode(Mode m)    noexcept { mode = m; }
    void setCutoff(float hz) noexcept
    {
        cutoff = std::clamp(hz, 20.0f, 20000.0f);
        updateCoef();
    }

    inline float process(float x) noexcept
    {
        // TPT one-pole low-pass
        const float v = (x - z) * g;
        const float lp = v + z;
        z = lp + v;
        return mode == Mode::LowPass ? lp : (x - lp);
    }

    void reset() noexcept { z = 0.0f; }

private:
    void updateCoef() noexcept
    {
        const float wd = 2.0f * 3.14159265358979323846f * cutoff;
        const float T  = 1.0f / (float) sampleRate;
        const float wa = (2.0f / T) * std::tan(wd * T * 0.5f);
        g = wa * T * 0.5f / (1.0f + wa * T * 0.5f);
    }

    double sampleRate = 44100.0;
    float  cutoff = 8000.0f;
    float  g = 0.0f, z = 0.0f;
    Mode   mode = Mode::LowPass;
};
