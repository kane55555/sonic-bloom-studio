#pragma once
//==============================================================================
//  DelayBlock.h — Stereo ping-pong-capable delay with feedback safety clamp
//  and built-in damping. Sample-rate aware. Up to 2 seconds of delay.
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

    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        if (mix <= 0.0001f) return;

        const int n = buffer.getNumSamples();
        auto* L = buffer.getWritePointer(0);
        auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : L;

        const float delaySamples = juce::jlimit(1.0f, static_cast<float>(sr * 2.0 - 1.0),
                                                timeSeconds * static_cast<float>(sr));

        for (int i = 0; i < n; ++i)
        {
            const float dryL = L[i];
            const float dryR = R[i];

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

            L[i] = dryL * (1.0f - mix) + fL * mix;
            R[i] = dryR * (1.0f - mix) + fR * mix;
        }
    }

    void reset() noexcept { delayLine.reset(); dampL.reset(); dampR.reset(); }

private:
    double sr = 44100.0;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 96000 };
    juce::dsp::FirstOrderTPTFilter<float> dampL, dampR;

    float timeSeconds = 0.3f;
    float feedback    = 0.4f;
    float mix         = 0.0f;
    float damping     = 6000.0f;
    bool  pingPong    = false;
};
