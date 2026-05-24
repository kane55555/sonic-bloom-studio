#pragma once
//==============================================================================
//  AnalogEngine.h — Subtractive analog-style oscillator bank.
//
//  Sources: saw / square / pulse / triangle / sine / noise + PWM
//  Sub-oscillator: square one octave below
//  Up to 8 unison voices with detune, stereo spread, phase randomisation
//  and slow per-voice drift.
//==============================================================================
#include "IEngineSource.h"
#include <array>
#include <random>

namespace dida { namespace engines {

class AnalogEngine : public IEngineSource
{
public:
    enum class Shape { Saw, Square, Pulse, Tri, Sine, Noise };

    EngineType type() const noexcept override { return EngineType::Analog; }

    void prepare(double sr, int /*blockSize*/) override
    {
        sampleRate = sr > 0 ? sr : 44100.0;
        reset();
    }

    void reset() override
    {
        for (auto& p : phases) p = 0.0;
        subPhase = 0.0;
    }

    void noteOn(int /*midi*/, float vel) override
    {
        velocity = vel;
        // Phase randomisation per unison voice for an analog feel.
        std::uniform_real_distribution<double> d(0.0, 1.0);
        for (auto& p : phases) p = d(rng);
        subPhase = d(rng);
    }
    void noteOff() override {}

    // Parameters
    void setShape(Shape s) noexcept             { shape = s; }
    void setUnison(int v) noexcept              { unison = juce::jlimit(1, 8, v); }
    void setDetune(float cents) noexcept        { detuneCents = juce::jlimit(0.0f, 50.0f, cents); }
    void setStereoSpread(float a) noexcept      { spread = juce::jlimit(0.0f, 1.0f, a); }
    void setPulseWidth(float pw) noexcept       { pulseWidth = juce::jlimit(0.05f, 0.95f, pw); }
    void setSubLevel(float v) noexcept          { subLevel = juce::jlimit(0.0f, 1.0f, v); }
    void setDrift(float c) noexcept             { driftCents = juce::jlimit(0.0f, 20.0f, c); }

    void renderAdd(float* outL, float* outR, int n, float pitchHz,
                   const ModSnapshot& mods) override
    {
        if (n <= 0) return;
        const double sr = sampleRate;
        const float vel = juce::jlimit(0.0f, 1.0f, velocity);
        const float invU = 1.0f / juce::jmax(1, unison);

        for (int i = 0; i < n; ++i)
        {
            float l = 0.0f, r = 0.0f;
            for (int v = 0; v < unison; ++v)
            {
                const float t = (unison == 1) ? 0.0f : (float(v) / float(unison - 1) - 0.5f) * 2.0f;
                const float vCents = t * detuneCents + mods.pitchDriftCents + driftCents * std::sin((float) phases[v] * juce::MathConstants<float>::twoPi * 0.13f);
                const float fHz = pitchHz * std::pow(2.0f, vCents / 1200.0f);
                phases[v] += fHz / sr;
                if (phases[v] >= 1.0) phases[v] -= 1.0;

                const float s = renderShape((float) phases[v]);

                // equal-power stereo spread per voice
                const float pan = t * spread;
                const float gL = std::cos((pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi);
                const float gR = std::sin((pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi);
                l += s * gL;
                r += s * gR;
            }
            l *= invU * vel;
            r *= invU * vel;

            if (subLevel > 0.0f)
            {
                subPhase += (pitchHz * 0.5) / sr;
                if (subPhase >= 1.0) subPhase -= 1.0;
                const float sq = subPhase < 0.5 ? 1.0f : -1.0f;
                const float s = sq * subLevel * vel * 0.7f;
                l += s; r += s;
            }

            outL[i] += l;
            outR[i] += r;
        }
    }

private:
    float renderShape(float p) noexcept
    {
        switch (shape)
        {
            case Shape::Saw:    return 2.0f * p - 1.0f;
            case Shape::Square: return p < 0.5f ? 1.0f : -1.0f;
            case Shape::Pulse:  return p < pulseWidth ? 1.0f : -1.0f;
            case Shape::Tri:    return 4.0f * std::fabs(p - 0.5f) - 1.0f;
            case Shape::Sine:   return std::sin(p * juce::MathConstants<float>::twoPi);
            case Shape::Noise:
            {
                std::uniform_real_distribution<float> d(-1.0f, 1.0f);
                return d(rng);
            }
        }
        return 0.0f;
    }

    double sampleRate = 44100.0;
    Shape  shape = Shape::Saw;
    int    unison = 1;
    float  detuneCents = 0.0f;
    float  spread = 0.0f;
    float  pulseWidth = 0.5f;
    float  subLevel = 0.0f;
    float  driftCents = 0.0f;
    float  velocity = 1.0f;
    std::array<double, 8> phases {};
    double subPhase = 0.0;
    std::mt19937 rng { 0xA1A1B2B2u };
};

}} // namespace dida::engines
