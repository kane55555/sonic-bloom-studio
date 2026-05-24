#pragma once
//==============================================================================
//  VoiceRandomizer.h — Per-note randomization helpers.
//  Picks tiny offsets (pitch cents, cutoff, env timing, pan, start delay) so
//  no two notes are bit-identical. Keep amounts SMALL — these are character,
//  not effects.
//==============================================================================
#include <cstdint>

namespace dida {

struct PerNoteJitter
{
    float pitchCents      = 0.0f;  // ±depth*4 cents
    float cutoffSemis     = 0.0f;  // ±depth*2 semitones of filter cutoff
    float attackScale     = 1.0f;  // 1 ± depth*0.10
    float releaseScale    = 1.0f;  // 1 ± depth*0.10
    float panOffset       = 0.0f;  // ±depth*0.05
    int   startDelaySamps = 0;     // 0..depth*128 samples
};

class VoiceRandomizer
{
public:
    void setSeed(std::uint32_t s) noexcept { state = (s == 0 ? 0xA5A5A5u : s); }
    void setAmount(float a) noexcept       { amount = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a); }

    PerNoteJitter pick(int sampleRateHz) noexcept
    {
        PerNoteJitter j;
        if (amount <= 0.0f) return j;
        j.pitchCents      = sym(4.0f);
        j.cutoffSemis     = sym(2.0f);
        j.attackScale     = 1.0f + sym(0.10f);
        j.releaseScale    = 1.0f + sym(0.10f);
        j.panOffset       = sym(0.05f);
        const float maxDelaySec = 0.003f * amount; // up to 3 ms
        j.startDelaySamps = (int)(uni() * maxDelaySec * sampleRateHz);
        return j;
    }

private:
    float sym(float m) noexcept { return (uni() * 2.0f - 1.0f) * m * amount; }
    float uni() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return ((state >> 8) & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
    }
    std::uint32_t state = 0xA5A5A5u;
    float amount = 0.25f;
};

} // namespace dida
