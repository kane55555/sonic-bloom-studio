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
#include "../Envelope.h"
#include <array>
#include <atomic>
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
        ampEnv.prepare(sampleRate);
        reset();
    }

    void reset() override
    {
        for (auto& p : phases) p = 0.0;
        subPhase = 0.0;
        ampEnv.reset();
        lastPeak.store(0.0f);
        voiceStarted.store(false);
        renderedBlock.store(false);
        envStage.store((int) ADSREnvelope::Stage::Idle);
    }

    void noteOn(int /*midi*/, float vel) override
    {
        velocity = vel;
        // Phase randomisation per unison voice for an analog feel.
        std::uniform_real_distribution<double> d(0.0, 1.0);
        for (auto& p : phases) p = d(rng);
        subPhase = d(rng);
        ampEnv.noteOn();
        voiceStarted.store(true);
        // A fresh note has not produced a rendered block yet; the reporter waits
        // for renderAdd() to flip this true before it trusts support-body meters.
        renderedBlock.store(false);
        envStage.store((int) ADSREnvelope::Stage::Attack);
    }
    void noteOff() override { ampEnv.noteOff(); }

    // Parameters
    void setShape(Shape s) noexcept             { shape = s; }
    void setUnison(int v) noexcept              { unison = juce::jlimit(1, 8, v); }
    void setDetune(float cents) noexcept        { detuneCents = juce::jlimit(0.0f, 50.0f, cents); }
    void setStereoSpread(float a) noexcept      { spread = juce::jlimit(0.0f, 1.0f, a); }
    void setPulseWidth(float pw) noexcept       { pulseWidth = juce::jlimit(0.05f, 0.95f, pw); }
    void setSubLevel(float v) noexcept          { subLevel = juce::jlimit(0.0f, 1.0f, v); }
    void setDrift(float c) noexcept             { driftCents = juce::jlimit(0.0f, 20.0f, c); }
    void setAmpEnvelopeMs(float attackMs, float decayMs, float sustain, float releaseMs) noexcept
    {
        ampEnv.setAttack (juce::jlimit(0.5f, 2000.0f, attackMs)  * 0.001f);
        ampEnv.setDecay  (juce::jlimit(1.0f, 4000.0f, decayMs)   * 0.001f);
        ampEnv.setSustain(juce::jlimit(0.0f, 1.0f, sustain));
        ampEnv.setRelease(juce::jlimit(1.0f, 4000.0f, releaseMs) * 0.001f);
    }
    float getLastPeakLinear() const noexcept override { return lastPeak.load(); }
    float getStaticPeakLinear() const noexcept override { return voiceStarted.load() ? 1.0f : 0.0f; }

    juce::String getEnvelopeStateName() const noexcept override
    {
        switch ((ADSREnvelope::Stage) envStage.load())
        {
            case ADSREnvelope::Stage::Attack:  return "attack";
            case ADSREnvelope::Stage::Decay:   return "decay";
            case ADSREnvelope::Stage::Sustain: return "sustain";
            case ADSREnvelope::Stage::Release: return "release";
            case ADSREnvelope::Stage::Idle:
            default:                           return "idle";
        }
    }

    bool hasRenderedBlockSinceNoteOn() const noexcept override { return renderedBlock.load(); }

    void renderAdd(float* outL, float* outR, int n, float pitchHz,
                   const ModSnapshot& mods) override
    {
        if (n <= 0 || ! ampEnv.isActive()) return;
        const double sr = sampleRate;
        const float vel = juce::jlimit(0.0f, 1.0f, velocity);
        const float invU = 1.0f / juce::jmax(1, unison);

        float blockPeak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float env = ampEnv.getNextSample();
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
            l *= invU * vel * env;
            r *= invU * vel * env;

            if (subLevel > 0.0f)
            {
                subPhase += (pitchHz * 0.5) / sr;
                if (subPhase >= 1.0) subPhase -= 1.0;
                const float sq = subPhase < 0.5 ? 1.0f : -1.0f;
                const float s = sq * subLevel * vel * env * 0.7f;
                l += s; r += s;
            }

            outL[i] += l;
            outR[i] += r;
            blockPeak = juce::jmax(blockPeak, std::abs(l), std::abs(r));
        }
        lastPeak.store(blockPeak);
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
    ADSREnvelope ampEnv;
    std::atomic<float> lastPeak { 0.0f };
    std::atomic<bool> voiceStarted { false };
};

}} // namespace dida::engines
