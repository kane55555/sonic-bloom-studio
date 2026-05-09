#pragma once
//==============================================================================
//  ChorusBlock.h — Stereo chorus wrapping juce::dsp::Chorus with safer
//  defaults and a unified setter API consistent with the other FX blocks.
//==============================================================================
#include <JuceHeader.h>

class ChorusBlock
{
public:
    void prepare(double sampleRate, int samplesPerBlock) noexcept
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate       = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels      = 2;
        chorus.prepare(spec);
        chorus.setCentreDelay(7.0f);   // ms — classic chorus territory
        chorus.setFeedback(0.0f);
        chorus.setRate(rateHz);
        chorus.setDepth(depth);
        chorus.setMix(mix);
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        if (mix <= 0.0001f) return;
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        chorus.process(ctx);
    }

    void setRate (float hz)  noexcept { rateHz = juce::jlimit(0.05f, 8.0f, hz); chorus.setRate(rateHz); }
    void setDepth(float d)   noexcept { depth  = juce::jlimit(0.0f, 1.0f, d);   chorus.setDepth(depth); }
    void setMix  (float m)   noexcept { mix    = juce::jlimit(0.0f, 1.0f, m);   chorus.setMix(mix); }

    void reset() noexcept { chorus.reset(); }

private:
    juce::dsp::Chorus<float> chorus;
    float rateHz = 1.0f;
    float depth  = 0.25f;
    float mix    = 0.0f;
};
