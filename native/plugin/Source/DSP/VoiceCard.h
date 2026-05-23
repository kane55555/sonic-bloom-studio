#pragma once
//==============================================================================
//  VoiceCard.h — 8 persistent "analog voice card" calibration profiles.
//
//  Each polyphonic voice borrows one card so it behaves like a slightly
//  different physical voice card (Juno/Jupiter/Prophet-style imperfections).
//  Cards are generated once with a fixed seed so the same card sounds the
//  same across sessions. A global `vintageAmount` (0..1) scales every
//  offset from "clean/modern" (0) to "unstable/vintage" (1).
//==============================================================================
#include <array>
#include <cmath>
#include <cstdint>

namespace dida {

struct VoiceCard
{
    float pitchCents      = 0.0f;  // ±0.5..±4 cents
    float pulseWidthOff   = 0.0f;  // ±0.01..±0.04
    float gainDb          = 0.0f;  // ±0.2..±0.8 dB
    float cutoffHz        = 0.0f;  // ±30..±180 Hz
    float resonanceOff    = 0.0f;  // ±0.01..±0.05
    float envAttackScale  = 1.0f;  // ±2..±8%
    float envDecayScale   = 1.0f;  // ±2..±10%
    float envReleaseScale = 1.0f;  // ±2..±10%
    float vcaGainDb       = 0.0f;  // ±0.2..±0.7 dB
    float panOffset       = 0.0f;  // ±0.02..±0.08
    float driftHz         = 0.07f; // 0.03..0.25 Hz slow analog drift
    float driftPhase      = 0.0f;  // start phase 0..1
};

class VoiceCardBank
{
public:
    static constexpr int NUM_CARDS = 8;

    static const VoiceCardBank& instance() noexcept
    {
        static VoiceCardBank inst;
        return inst;
    }

    const VoiceCard& get(int index) const noexcept
    {
        return cards[((index % NUM_CARDS) + NUM_CARDS) % NUM_CARDS];
    }

private:
    VoiceCardBank() noexcept
    {
        // Deterministic LCG so cards are stable across builds/sessions.
        std::uint32_t s = 0xC0FFEE13u;
        auto rnd = [&s]() {
            s = s * 1664525u + 1013904223u;
            return (s >> 8) & 0xFFFFFF;
        };
        auto sym = [&](float maxAbs) {
            const float u = ((float) rnd() / (float) 0xFFFFFF) * 2.0f - 1.0f;
            return u * maxAbs;
        };
        auto rng = [&](float lo, float hi) {
            const float u = (float) rnd() / (float) 0xFFFFFF;
            return lo + u * (hi - lo);
        };

        for (int i = 0; i < NUM_CARDS; ++i)
        {
            VoiceCard c;
            c.pitchCents      = sym(4.0f);
            // Bias away from zero so even at low vintage there's life.
            if (std::fabs(c.pitchCents) < 0.5f) c.pitchCents += (c.pitchCents >= 0 ? 0.5f : -0.5f);
            c.pulseWidthOff   = sym(0.04f);
            c.gainDb          = sym(0.8f);
            c.cutoffHz        = sym(180.0f);
            c.resonanceOff    = sym(0.05f);
            c.envAttackScale  = 1.0f + sym(0.08f);
            c.envDecayScale   = 1.0f + sym(0.10f);
            c.envReleaseScale = 1.0f + sym(0.10f);
            c.vcaGainDb       = sym(0.7f);
            c.panOffset       = sym(0.08f);
            c.driftHz         = rng(0.03f, 0.25f);
            c.driftPhase      = rng(0.0f, 1.0f);
            cards[i] = c;
        }
    }

    std::array<VoiceCard, NUM_CARDS> cards;
};

// Linearly interpolates a calibration value between "clean" (=0 or =1 for
// scale fields) and the card's full vintage offset by `amount` in 0..1.
inline float vintageMix(float fullOffset, float amount) noexcept
{
    if (amount <= 0.0f) return 0.0f;
    if (amount >= 1.0f) return fullOffset;
    return fullOffset * amount;
}

inline float vintageScale(float fullScale, float amount) noexcept
{
    // fullScale is e.g. 1.08; at amount=0 we want 1.0, at amount=1 we want fullScale.
    return 1.0f + (fullScale - 1.0f) * juce_min(juce_max(amount, 0.0f), 1.0f);
}

// Avoid pulling in JUCE just for jmin/jmax in this header.
inline float juce_min(float a, float b) noexcept { return a < b ? a : b; }
inline float juce_max(float a, float b) noexcept { return a > b ? a : b; }

} // namespace dida
