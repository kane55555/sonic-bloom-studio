#pragma once
//==============================================================================
//  AnalogDrift.h — Slow random walk used to wobble pitch/cutoff/pan.
//  Subtle by design: feed depth in cents, Hz, or fraction; depth=0 disables.
//  Each instance keeps its own LCG so different voices drift differently.
//==============================================================================
#include <cmath>
#include <cstdint>

namespace dida {

class AnalogDrift
{
public:
    void prepare(double sr) noexcept { sampleRate = sr; }
    void setRateHz(float hz) noexcept { rateHz = hz < 0.001f ? 0.001f : hz; }
    void setSeed(std::uint32_t s) noexcept { state = (s == 0 ? 0xC0FFEEu : s); }

    // One smoothed bipolar value in [-1, 1] per sample.
    inline float tick() noexcept
    {
        phase += rateHz / static_cast<float>(sampleRate);
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            // pick a new random target
            state = state * 1664525u + 1013904223u;
            const float u = ((state >> 8) & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
            target = u * 2.0f - 1.0f;
        }
        // Smooth toward target like a slow filtered noise.
        current += (target - current) * 0.002f;
        return current;
    }

private:
    double sampleRate = 44100.0;
    float  rateHz = 0.18f;
    float  phase = 0.0f, current = 0.0f, target = 0.0f;
    std::uint32_t state = 0xC0FFEEu;
};

} // namespace dida
