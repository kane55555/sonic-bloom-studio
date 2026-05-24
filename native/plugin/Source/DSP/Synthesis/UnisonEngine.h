#pragma once
//==============================================================================
//  UnisonEngine.h — Real 2..8 voice unison stack for synth-layer rendering.
//
//  This is the supersaw-style core used when SynthVoice falls back to pure
//  oscillator synthesis (no sample) and also as the body oscillator for
//  hybrid presets. Each unit has:
//      - independent detune (cents)
//      - independent stereo pan (equal-power)
//      - independent randomized start phase (kills static "stacked" attack)
//      - slow per-unit drift (vintage instability)
//
//  Output is stereo. CPU is O(numVoices) per sample; default = 1 voice (off).
//==============================================================================
#include <array>
#include <cmath>
#include <random>

namespace dida {

class UnisonEngine
{
public:
    static constexpr int MAX_VOICES = 8;

    void prepare(double sr) noexcept
    {
        sampleRate = sr;
        randomizePhasesAndDrift();
    }

    // Configure stack. detune: 0..1 (0 = unison, 1 = ±30 cents).
    // spread: 0..1 stereo width. drift: 0..1 slow analog instability.
    void setConfig(int voices, float detune, float spread, float drift) noexcept
    {
        numVoices = voices < 1 ? 1 : (voices > MAX_VOICES ? MAX_VOICES : voices);
        detuneAmt = clamp01(detune);
        spreadAmt = clamp01(spread);
        driftAmt  = clamp01(drift);
        recomputeOffsets();
    }

    // Call on every noteOn so each note gets a fresh phase scramble
    // (anti-static / anti-machine-gun).
    void randomizePhasesAndDrift() noexcept
    {
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        for (int i = 0; i < MAX_VOICES; ++i)
        {
            phases[i]     = u(rng);
            driftPhases[i] = u(rng);
            driftRates[i]  = 0.04f + u(rng) * 0.18f;  // 0.04..0.22 Hz
        }
    }

    // Render one stereo sample of a saw stack at fundamental hz.
    // (Saw is the workhorse shape; chooseShape lets you pick others.)
    enum class Shape { Saw, Square, Triangle };

    inline void renderSample(float hz, Shape shape, float& outL, float& outR) noexcept
    {
        outL = outR = 0.0f;
        const float inv = 1.0f / static_cast<float>(numVoices);
        for (int i = 0; i < numVoices; ++i)
        {
            // Vintage drift wiggle in cents.
            driftPhases[i] += driftRates[i] / static_cast<float>(sampleRate);
            if (driftPhases[i] >= 1.0f) driftPhases[i] -= 1.0f;
            const float driftCents = std::sin(driftPhases[i] * 6.2831853f) * (3.0f * driftAmt);

            const float ratio = std::pow(2.0f, (detuneCents[i] + driftCents) / 1200.0f);
            const float inc   = (hz * ratio) / static_cast<float>(sampleRate);
            phases[i] += inc;
            if (phases[i] >= 1.0f) phases[i] -= 1.0f;

            float v = 0.0f;
            const float p = phases[i];
            switch (shape)
            {
                case Shape::Saw:      v = 2.0f * (p - std::floor(p + 0.5f)); break;
                case Shape::Square:   v = (p < 0.5f) ? 1.0f : -1.0f; break;
                case Shape::Triangle: v = 4.0f * std::abs(p - std::floor(p + 0.5f)) - 1.0f; break;
            }

            outL += v * panL[i];
            outR += v * panR[i];
        }
        outL *= inv;
        outR *= inv;
    }

    int currentVoices() const noexcept { return numVoices; }

private:
    static float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    void recomputeOffsets() noexcept
    {
        // Symmetric detune fan: -range..+range cents, centred on 0.
        const float range = 30.0f * detuneAmt; // up to ±30c
        for (int i = 0; i < numVoices; ++i)
        {
            const float t = (numVoices == 1) ? 0.0f
                          : (static_cast<float>(i) / (numVoices - 1)) * 2.0f - 1.0f;
            detuneCents[i] = t * range;
            // Equal-power pan: edges pushed to ±spread.
            const float pan = t * spreadAmt;
            const float ang = (pan + 1.0f) * 0.25f * 3.14159265f;
            panL[i] = std::cos(ang) * 1.41421356f;
            panR[i] = std::sin(ang) * 1.41421356f;
        }
        for (int i = numVoices; i < MAX_VOICES; ++i)
        { detuneCents[i] = 0.0f; panL[i] = panR[i] = 0.7071f; }
    }

    double sampleRate = 44100.0;
    int    numVoices  = 1;
    float  detuneAmt  = 0.0f, spreadAmt = 0.0f, driftAmt = 0.0f;

    std::array<float, MAX_VOICES> phases       {};
    std::array<float, MAX_VOICES> detuneCents  {};
    std::array<float, MAX_VOICES> panL         {};
    std::array<float, MAX_VOICES> panR         {};
    std::array<float, MAX_VOICES> driftPhases  {};
    std::array<float, MAX_VOICES> driftRates   {};

    std::mt19937 rng { 0xDEADBEEFu };
};

} // namespace dida
