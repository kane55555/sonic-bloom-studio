#pragma once
//==============================================================================
//  HarmonicExciter.h — Generates upper harmonics for premium tone.
//
//  Splits signal into low + high band; pushes the high band through a
//  combination of soft-clip + asymmetric tanh + tiny wavefold. Sums back in.
//  At low amount this just sweetens; pushed it becomes a tape-style sheen.
//==============================================================================
#include <cmath>

namespace dida {

class HarmonicExciter
{
public:
    void prepare(double sr) noexcept
    {
        sampleRate = sr;
        // 1-pole crossover at 2.4 kHz — split body from sheen band.
        const float fc = 2400.0f;
        a = std::exp(-2.0f * 3.14159265f * fc / static_cast<float>(sampleRate));
        zL = zR = 0.0f;
    }

    void setAmount(float amt) noexcept { amount = amt < 0.0f ? 0.0f : (amt > 1.0f ? 1.0f : amt); }
    void setBias  (float b)   noexcept { bias = b; } // asymmetric warmth

    inline void process(float& l, float& r) noexcept
    {
        if (amount <= 0.0001f) return;
        // Low band via 1-pole LP; high band = input - low.
        zL = (1.0f - a) * l + a * zL;
        zR = (1.0f - a) * r + a * zR;
        const float hiL = l - zL;
        const float hiR = r - zR;

        const float drive = 1.0f + amount * 6.0f;
        // Asymmetric tanh = even+odd harmonics → "tube" character.
        const float exL = std::tanh(hiL * drive + bias * amount) - std::tanh(bias * amount);
        const float exR = std::tanh(hiR * drive + bias * amount) - std::tanh(bias * amount);
        // Subtle wavefold on the excited high band (clamped).
        auto fold = [](float x) {
            while (x >  1.0f) x =  2.0f - x;
            while (x < -1.0f) x = -2.0f - x;
            return x;
        };
        const float blend = amount * 0.6f;
        l = zL + hiL + (fold(exL) - hiL) * blend;
        r = zR + hiR + (fold(exR) - hiR) * blend;
    }

private:
    double sampleRate = 44100.0;
    float  a = 0.0f, zL = 0.0f, zR = 0.0f;
    float  amount = 0.0f, bias = 0.05f;
};

} // namespace dida
