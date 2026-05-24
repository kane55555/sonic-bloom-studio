#pragma once
//==============================================================================
//  SupersawEngine.h — 3..9 detuned sawtooth voices for big Roland/Nexus-style
//  leads and pads. Spread + per-voice phase randomisation + analog drift.
//==============================================================================
#include "IEngineSource.h"
#include <array>
#include <random>

namespace dida { namespace engines {

class SupersawEngine : public IEngineSource
{
public:
    EngineType type() const noexcept override { return EngineType::Supersaw; }

    void prepare(double sr, int) override { sampleRate = sr > 0 ? sr : 44100.0; reset(); }
    void reset() override { for (auto& p : phases) p = 0.0; }
    void noteOn(int, float vel) override
    {
        velocity = vel;
        std::uniform_real_distribution<double> d(0.0, 1.0);
        for (auto& p : phases) p = d(rng);
    }
    void noteOff() override {}

    void setVoices(int v) noexcept          { voices = juce::jlimit(3, 9, v); }
    void setDetuneCents(float c) noexcept   { detuneCents = juce::jlimit(0.0f, 50.0f, c); }
    void setSpread(float a) noexcept        { spread = juce::jlimit(0.0f, 1.0f, a); }
    void setDriftCents(float c) noexcept    { driftCents = juce::jlimit(0.0f, 15.0f, c); }
    // Detune curve: 0 = linear, 1 = JP-8000-style "smile" (more detune at extremes).
    void setDetuneCurve(float c) noexcept   { curve = juce::jlimit(0.0f, 1.0f, c); }

    void renderAdd(float* outL, float* outR, int n, float pitchHz,
                   const ModSnapshot& mods) override
    {
        const double sr = sampleRate;
        const float vel = juce::jlimit(0.0f, 1.0f, velocity);
        const float inv = 1.0f / float(voices);

        for (int i = 0; i < n; ++i)
        {
            float l = 0.0f, r = 0.0f;
            for (int v = 0; v < voices; ++v)
            {
                const float t = (voices == 1) ? 0.0f : (float(v) / float(voices - 1) - 0.5f) * 2.0f;
                const float shaped = (1.0f - curve) * t + curve * (t * std::fabs(t));
                const float cents = shaped * detuneCents + mods.pitchDriftCents
                                  + driftCents * std::sin((float) phases[v] * juce::MathConstants<float>::twoPi * 0.07f);
                const float fHz = pitchHz * std::pow(2.0f, cents / 1200.0f);
                phases[v] += fHz / sr;
                if (phases[v] >= 1.0) phases[v] -= 1.0;

                const float saw = 2.0f * (float) phases[v] - 1.0f;
                const float pan = t * spread;
                const float gL = std::cos((pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi);
                const float gR = std::sin((pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi);
                l += saw * gL;
                r += saw * gR;
            }
            outL[i] += l * inv * vel;
            outR[i] += r * inv * vel;
        }
    }

private:
    double sampleRate = 44100.0;
    int    voices = 7;
    float  detuneCents = 18.0f;
    float  spread = 0.85f;
    float  driftCents = 3.0f;
    float  curve = 0.4f;
    float  velocity = 1.0f;
    std::array<double, 9> phases {};
    std::mt19937 rng { 0x51517272u };
};

}} // namespace dida::engines
