#pragma once
//==============================================================================
//  UtilityDSP.h — Shared DSP helpers for DIDITAGAIN STUDIO.
//
//  All helpers are sample-rate aware where relevant. Designed to be small,
//  branch-free in hot paths, and safe by default (no NaNs, no hard clips).
//==============================================================================
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace dida {

// ---- Constants -------------------------------------------------------------
constexpr float kPi      = 3.14159265358979323846f;
constexpr float kTwoPi   = 6.28318530717958647692f;
constexpr float kInvPi   = 1.0f / kPi;
constexpr float kSqrt2   = 1.41421356237309504880f;

// ---- MIDI / pitch helpers --------------------------------------------------
inline float midiToHz(float midiNote, float a4Hz = 440.0f) noexcept
{
    return a4Hz * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
}

inline float centsToRatio(float cents) noexcept
{
    return std::pow(2.0f, cents / 1200.0f);
}

// ---- Gain conversion -------------------------------------------------------
inline float dbToGain(float db) noexcept
{
    return std::pow(10.0f, db * 0.05f);
}

inline float gainToDb(float gain, float floorDb = -100.0f) noexcept
{
    return gain > 1e-6f ? 20.0f * std::log10(gain) : floorDb;
}

// ---- Safe / soft-clip ------------------------------------------------------
// Very cheap soft saturator that never exceeds ±1 — used as a final safety net.
inline float softClip(float x) noexcept
{
    if (x >  1.5f) return  1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (x * x * x) * (1.0f / 6.75f); // tanh-ish, smooth at ±1
}

inline float tanhClip(float x) noexcept { return std::tanh(x); }

inline float denormalGuard(float x) noexcept
{
    return std::abs(x) < 1.0e-20f ? 0.0f : x;
}

// ---- One-pole parameter smoother ------------------------------------------
class OnePoleSmoother
{
public:
    void prepare(double sampleRate, float smoothingMs = 20.0f) noexcept
    {
        sr = sampleRate;
        setTimeMs(smoothingMs);
    }
    void setTimeMs(float ms) noexcept
    {
        const float tau = std::max(0.1f, ms) * 0.001f;
        coeff = std::exp(-1.0f / (static_cast<float>(sr) * tau));
    }
    void setTarget(float t) noexcept { target = t; }
    void snap(float v) noexcept     { target = current = v; }
    float next() noexcept
    {
        current = target + (current - target) * coeff;
        return denormalGuard(current);
    }
    float getCurrent() const noexcept { return current; }

private:
    double sr     = 44100.0;
    float  coeff  = 0.99f;
    float  target = 0.0f;
    float  current = 0.0f;
};

// ---- Pink noise (Voss-McCartney lite) -------------------------------------
class PinkNoise
{
public:
    float next() noexcept
    {
        // Very cheap pink-ish filter on white noise.
        const float white = whiteNoise();
        b0 = 0.99886f * b0 + white * 0.0555179f;
        b1 = 0.99332f * b1 + white * 0.0750759f;
        b2 = 0.96900f * b2 + white * 0.1538520f;
        const float pink = (b0 + b1 + b2 + white * 0.1848f) * 0.25f;
        return denormalGuard(pink);
    }

    float whiteNoise() noexcept
    {
        // xorshift32 — fast, deterministic, no <random> dependency
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (static_cast<int32_t>(state) / static_cast<float>(0x7FFFFFFF));
    }

private:
    uint32_t state = 0x9E3779B9u;
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
};

} // namespace dida
