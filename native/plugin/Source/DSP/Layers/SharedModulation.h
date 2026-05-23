#pragma once
//==============================================================================
//  SharedModulation.h — Slow shared LFOs that all layers can subscribe to.
//
//  A single LFO advanced once per block (or per sample) produces a value
//  that the bus processor uses for stereo drift and the carver cutoffs use
//  for filter movement. The point is that every layer moves together —
//  unified motion is what sells "one instrument" instead of "stacked samples".
//==============================================================================
#include <cmath>

class SharedLFO
{
public:
    void prepare(double sr) noexcept { sampleRate = sr; }
    void setRateHz(float hz) noexcept { rateHz = hz; }
    void setDepth (float d)  noexcept { depth = d; }

    inline float tick() noexcept
    {
        phase += rateHz / (float) sampleRate;
        if (phase >= 1.0f) phase -= 1.0f;
        return std::sin(phase * 6.283185307179586f) * depth;
    }

    float currentPhase() const noexcept { return phase; }

private:
    double sampleRate = 44100.0;
    float  rateHz = 0.18f;
    float  depth  = 1.0f;
    float  phase  = 0.0f;
};
