#pragma once
//==============================================================================
//  DelayBlock.h — Stereo ping-pong-capable delay with feedback safety clamp
//  and built-in damping. Sample-rate aware. Up to 2 seconds of delay.
//
//  Scale-safety extensions:
//    - Dry-triggered ducking: incoming notes briefly tuck the wet echoes so
//      fast melodies/scales don't pile up on top of each other.
//    - Density send reduction: an external multiplier (0..1) lowers wet send
//      when many notes are played close together (set from FxChain's note
//      onset tracker).
//==============================================================================
#include <JuceHeader.h>
#include "UtilityDSP.h"

class DelayBlock
{
public:
    void prepare(double sampleRate, int samplesPerBlock) noexcept
    {
        sr = sampleRate;
        const int maxSamples = static_cast<int>(sampleRate * 2.0) + 8;
        juce::dsp::ProcessSpec spec { sampleRate,
                                      static_cast<juce::uint32>(samplesPerBlock),
                                      2 };
        delayLine.reset();
        delayLine.prepare(spec);
        delayLine.setMaximumDelayInSamples(maxSamples);

        dampL.reset();
        dampR.reset();
        dampL.prepare(spec); dampR.prepare(spec);
        dampL.setType(juce::dsp::FirstOrderTPTFilterType::lowpass);
        dampR.setType(juce::dsp::FirstOrderTPTFilterType::lowpass);
        dampL.setCutoffFrequency(damping); dampR.setCutoffFrequency(damping);

        recalcDuckCoefs();
        duckEnv = 0.0f;
    }

    void setTimeSeconds(float seconds) noexcept
    {
        timeSeconds = juce::jlimit(0.001f, 2.0f, seconds);
    }
    void setFeedback(float f)  noexcept { feedback = juce::jlimit(0.0f, 0.92f, f); } // hard cap to prevent runaway
    void setMix(float m)       noexcept { mix      = juce::jlimit(0.0f, 1.0f,  m); }
    void setPingPong(bool on)  noexcept { pingPong = on; }
    void setDampingHz(float hz) noexcept
    {
        damping = juce::jlimit(200.0f, 18000.0f, hz);
        dampL.setCutoffFrequency(damping);
        dampR.setCutoffFrequency(damping);
    }

    /** Dry-signal-driven ducking on the wet echoes.
        amount=0..0.7 — fractional gain reduction at full envelope.
        attackMs default 5ms = fast tuck; releaseMs default 140ms = settles back. */
    void setDucking(float amount, float attackMs = 5.0f, float releaseMs = 140.0f) noexcept
    {
        duckAmount    = juce::jlimit(0.0f, 0.7f, amount);
        duckAttackMs  = juce::jlimit(1.0f, 40.0f, attackMs);
        duckReleaseMs = juce::jlimit(20.0f, 600.0f, releaseMs);
        recalcDuckCoefs();
    }

    /** External 0..1 multiplier applied to the wet send. FxChain pulls this
        down during dense playing to keep scales clean. */
    void setSendDensityScale(float s) noexcept
    {
        sendDensityScale = juce::jlimit(0.0f, 1.0f, s);
    }

    // Live state accessors for the preset-quality reporter.
    float getMix()          const noexcept { return mix; }
    float getFeedback()     const noexcept { return feedback; }
    float getDuckAmount()   const noexcept { return duckAmount; }
    float getDuckAttackMs() const noexcept { return duckAttackMs; }
    float getDuckReleaseMs()const noexcept { return duckReleaseMs; }

    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        const float effectiveMix = mix * sendDensityScale;
        if (effectiveMix <= 0.0001f) return;

        const int n = buffer.getNumSamples();
        auto* L = buffer.getWritePointer(0);
        auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : L;

        const float delaySamples = juce::jlimit(1.0f, static_cast<float>(sr * 2.0 - 1.0),
                                                timeSeconds * static_cast<float>(sr));

        for (int i = 0; i < n; ++i)
        {
            const float dryL = L[i];
            const float dryR = R[i];

            // Dry-signal envelope follower for ducking.
            const float detector = juce::jmin(1.0f,
                                              0.5f * (std::abs(dryL) + std::abs(dryR)) * 2.2f);
            const float coef = detector > duckEnv ? duckAttackCoef : duckReleaseCoef;
            duckEnv = detector + coef * (duckEnv - detector);
            const float duckGain = juce::jlimit(0.30f, 1.0f, 1.0f - duckAmount * duckEnv);

            const float dL = delayLine.popSample(0, delaySamples, true);
            const float dR = delayLine.popSample(1, delaySamples, true);

            const float fL = dampL.processSample(0, dL);
            const float fR = dampR.processSample(0, dR);

            float inL, inR;
            if (pingPong)
            {
                inL = dryL + fR * feedback;
                inR = dryR + fL * feedback;
            }
            else
            {
                inL = dryL + fL * feedback;
                inR = dryR + fR * feedback;
            }

            delayLine.pushSample(0, dida::denormalGuard(inL));
            delayLine.pushSample(1, dida::denormalGuard(inR));

            const float wet = effectiveMix * duckGain;
            L[i] = dryL * (1.0f - effectiveMix) + fL * wet;
            R[i] = dryR * (1.0f - effectiveMix) + fR * wet;
        }
    }

    void processWetOnly(juce::AudioBuffer<float>& buffer,
                        const juce::AudioBuffer<float>& dryInput) noexcept
    {
        const float effectiveMix = mix * sendDensityScale;
        if (effectiveMix <= 0.0001f)
        {
            buffer.clear();
            return;
        }

        process(buffer);

        const int n = buffer.getNumSamples();
        const int nc = juce::jmin(buffer.getNumChannels(), dryInput.getNumChannels());
        const float dryScale = 1.0f - effectiveMix;
        for (int ch = 0; ch < nc; ++ch)
            buffer.addFrom(ch, 0, dryInput, ch, 0, n, -dryScale);
    }

    void reset() noexcept { delayLine.reset(); dampL.reset(); dampR.reset(); duckEnv = 0.0f; }

private:
    void recalcDuckCoefs() noexcept
    {
        const auto msToCoef = [this](float ms) noexcept
        {
            return std::exp(-1.0f / std::max(1.0f, ms * 0.001f * (float) sr));
        };
        duckAttackCoef  = msToCoef(duckAttackMs);
        duckReleaseCoef = msToCoef(duckReleaseMs);
    }

    double sr = 44100.0;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 96000 };
    juce::dsp::FirstOrderTPTFilter<float> dampL, dampR;

    float timeSeconds = 0.3f;
    float feedback    = 0.4f;
    float mix         = 0.0f;
    float damping     = 6000.0f;
    bool  pingPong    = false;

    // Ducking + density-aware send.
    float duckAmount     = 0.0f;
    float duckAttackMs   = 5.0f;
    float duckReleaseMs  = 140.0f;
    float duckAttackCoef = 0.0f;
    float duckReleaseCoef= 0.0f;
    float duckEnv        = 0.0f;
    float sendDensityScale = 1.0f;
};
