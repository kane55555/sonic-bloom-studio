#pragma once
//==============================================================================
//  UnisonEngine.h — Premium 3..9 voice unison stack (JP-8000 style supersaw).
//
//  Per-unit:
//      - JP-8000 nonlinear detune fan + symmetric mirror around center
//      - dedicated centre voice at 0 cents, 0 pan, reinforced amplitude
//      - independent equal-power pan
//      - randomized start phase (anti-machine-gun)
//      - per-voice slow drift (vintage instability)
//      - polyBLEP-corrected saw/square (anti-aliased)
//
//  Stack:
//      - sqrt(N) gain normalization so a 9-voice stack matches a single saw
//      - optional warmth tilt (1-pole LP on the stereo sum) to take the
//        harsh top end off heavy detune
//
//  CPU is O(numVoices) per sample.
//==============================================================================
#include <array>
#include <cmath>
#include <random>

namespace dida {

class UnisonEngine
{
public:
    static constexpr int MAX_VOICES = 9;

    void prepare(double sr) noexcept
    {
        sampleRate = sr;
        randomizePhasesAndDrift();
        warmthZL = warmthZR = 0.0f;
    }

    // Configure stack.
    //   voices: 1..9 (3/5/7/9 are the musical choices)
    //   detune: 0..1 — JP-8000 nonlinear curve, 0 = unison, 1 = full spread
    //   spread: 0..1 stereo width
    //   drift : 0..1 slow analog instability
    void setConfig(int voices, float detune, float spread, float drift) noexcept
    {
        numVoices = voices < 1 ? 1 : (voices > MAX_VOICES ? MAX_VOICES : voices);
        detuneAmt = clamp01(detune);
        spreadAmt = clamp01(spread);
        driftAmt  = clamp01(drift);
        recomputeOffsets();
    }

    // 0 = bypass (no top-end tame), 1 = soft bedroom-producer warmth.
    void setWarmth(float w) noexcept
    {
        warmthAmt = clamp01(w);
        // Map warmth -> cutoff hz: 0 -> 18 kHz (effectively off), 1 -> 3 kHz.
        const float hz = 18000.0f - warmthAmt * 15000.0f;
        const float x  = std::exp(-2.0f * 3.14159265f * hz / static_cast<float>(sampleRate));
        warmthA = 1.0f - x;
    }

    // 0 = side voices match centre, 1 = side voices slightly louder (classic
    // JP-8000 "blend" feel where the centre fundamental sits a touch behind
    // the swarm).
    void setBlend(float b) noexcept { blendAmt = clamp01(b); recomputeGains(); }

    // Call on every noteOn so each note gets fresh phase scramble.
    void randomizePhasesAndDrift() noexcept
    {
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        for (int i = 0; i < MAX_VOICES; ++i)
        {
            phases[i]      = u(rng);
            driftPhases[i] = u(rng);
            driftRates[i]  = 0.04f + u(rng) * 0.18f;  // 0.04..0.22 Hz
        }
    }

    enum class Shape { Saw, Square, Triangle };

    inline void renderSample(float hz, Shape shape, float& outL, float& outR) noexcept
    {
        outL = outR = 0.0f;
        // Decorrelated voices sum incoherently → 1/sqrt(N) keeps perceived
        // loudness stable as voice count changes.
        const float inv = 1.0f / std::sqrt(static_cast<float>(numVoices));
        const float sr  = static_cast<float>(sampleRate);

        for (int i = 0; i < numVoices; ++i)
        {
            // Per-voice slow drift in cents.
            driftPhases[i] += driftRates[i] / sr;
            if (driftPhases[i] >= 1.0f) driftPhases[i] -= 1.0f;
            const float driftCents = std::sin(driftPhases[i] * 6.2831853f) * (3.0f * driftAmt);

            const float ratio = std::pow(2.0f, (detuneCents[i] + driftCents) / 1200.0f);
            const float inc   = (hz * ratio) / sr;
            phases[i] += inc;
            if (phases[i] >= 1.0f) phases[i] -= 1.0f;

            const float p = phases[i];
            float v = 0.0f;
            auto blep = [](float t, float dt) -> float
            {
                if (dt <= 0.0f) return 0.0f;
                if (t < dt)        { const float x = t / dt;          return x + x - x * x - 1.0f; }
                if (t > 1.0f - dt) { const float x = (t - 1.0f) / dt; return x * x + x + x + 1.0f; }
                return 0.0f;
            };
            switch (shape)
            {
                case Shape::Saw:
                    v = 2.0f * p - 1.0f;
                    v -= blep(p, inc);
                    break;
                case Shape::Square:
                {
                    v = (p < 0.5f) ? 1.0f : -1.0f;
                    v += blep(p, inc);
                    const float p2 = (p + 0.5f) - std::floor(p + 0.5f);
                    v -= blep(p2, inc);
                    break;
                }
                case Shape::Triangle:
                    v = 4.0f * std::abs(p - std::floor(p + 0.5f)) - 1.0f;
                    break;
            }

            v *= unitGain[i];
            outL += v * panL[i];
            outR += v * panR[i];
        }
        outL *= inv;
        outR *= inv;

        // Warmth tilt (1-pole LP) on the stereo sum.
        if (warmthAmt > 0.0001f)
        {
            warmthZL += warmthA * (outL - warmthZL);
            warmthZR += warmthA * (outR - warmthZR);
            outL = warmthZL;
            outR = warmthZR;
        }
    }

    int currentVoices() const noexcept { return numVoices; }

private:
    static float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    // JP-8000 supersaw detune curve (Adam Szabo paper, polynomial fit).
    // Input x = 0..1 detune knob; output is the per-voice cent multiplier.
    static float jp8000Curve(float x) noexcept
    {
        // Maps the detune knob into a perceptually-balanced cent spread.
        return (10028.7312891634f  * std::pow(x, 11.0f))
             - (50818.8652045924f  * std::pow(x, 10.0f))
             + (111363.4808673960f * std::pow(x, 9.0f))
             - (138150.6761080260f * std::pow(x, 8.0f))
             + (106649.6679158350f * std::pow(x, 7.0f))
             - (53046.9642751875f  * std::pow(x, 6.0f))
             + (17019.9518580080f  * std::pow(x, 5.0f))
             - (3425.0836591318f   * std::pow(x, 4.0f))
             + (404.2703938388f    * std::pow(x, 3.0f))
             - (24.1878824391f     * std::pow(x, 2.0f))
             + (0.6717417634f      * x);
    }

    void recomputeOffsets() noexcept
    {
        // Symmetric ratio fan: outer voices farther than inner ones.
        // Classic JP-8000 used 7 fixed offsets — generalise to N voices by
        // distributing relative-positions across [-1, 1] then weighting by
        // the JP curve. Centre voice (when N is odd) stays at 0 cents.
        const float spreadCents = jp8000Curve(detuneAmt) * 100.0f; // up to ~70c
        for (int i = 0; i < numVoices; ++i)
        {
            const float t = (numVoices == 1) ? 0.0f
                          : (static_cast<float>(i) / (numVoices - 1)) * 2.0f - 1.0f;
            // Sign-preserving cube → outer voices detune more, centre stays tight.
            const float curved = t * t * t * 0.5f + t * 0.5f;
            detuneCents[i] = curved * spreadCents;

            // Equal-power pan keyed off the same curve so detune+pan track.
            const float pan = curved * spreadAmt;
            const float ang = (pan + 1.0f) * 0.25f * 3.14159265f;
            panL[i] = std::cos(ang) * 1.41421356f;
            panR[i] = std::sin(ang) * 1.41421356f;
        }
        for (int i = numVoices; i < MAX_VOICES; ++i)
        {
            detuneCents[i] = 0.0f;
            panL[i] = panR[i] = 0.7071f;
        }
        recomputeGains();
    }

    void recomputeGains() noexcept
    {
        // Centre vs side balance: at blend=0 every voice is unity; at
        // blend=1 side voices are +3 dB relative to centre (the classic
        // JP-8000 swarm feel).
        const float sideBoost = 1.0f + blendAmt * 0.41f; // ~+3 dB at blend=1
        for (int i = 0; i < numVoices; ++i)
        {
            const float t = (numVoices == 1) ? 0.0f
                          : (static_cast<float>(i) / (numVoices - 1)) * 2.0f - 1.0f;
            const bool isCentre = std::abs(t) < 0.01f;
            unitGain[i] = isCentre ? 1.0f : sideBoost;
        }
        for (int i = numVoices; i < MAX_VOICES; ++i) unitGain[i] = 1.0f;
    }

    double sampleRate = 44100.0;
    int    numVoices  = 1;
    float  detuneAmt  = 0.0f, spreadAmt = 0.0f, driftAmt = 0.0f;
    float  warmthAmt  = 0.0f, warmthA = 0.0f;
    float  warmthZL = 0.0f, warmthZR = 0.0f;
    float  blendAmt   = 0.5f;

    std::array<float, MAX_VOICES> phases       {};
    std::array<float, MAX_VOICES> detuneCents  {};
    std::array<float, MAX_VOICES> panL         {};
    std::array<float, MAX_VOICES> panR         {};
    std::array<float, MAX_VOICES> unitGain     {};
    std::array<float, MAX_VOICES> driftPhases  {};
    std::array<float, MAX_VOICES> driftRates   {};

    std::mt19937 rng { 0xDEADBEEFu };
};

} // namespace dida
