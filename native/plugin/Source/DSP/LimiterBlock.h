#pragma once
//==============================================================================
//  LimiterBlock.h — Master safety limiter using juce::dsp::Limiter.
//==============================================================================
#include <JuceHeader.h>

class LimiterBlock
{
public:
    void prepare(double sampleRate, int samplesPerBlock) noexcept
    {
        juce::dsp::ProcessSpec spec { sampleRate,
                                      static_cast<juce::uint32>(samplesPerBlock), 2 };
        limiter.prepare(spec);
        limiter.setRelease(80.0f);
        limiter.setThreshold(ceilingDb);
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        limiter.process(ctx);
    }

    void setCeilingDb(float db) noexcept
    {
        ceilingDb = juce::jlimit(-12.0f, 0.0f, db);
        limiter.setThreshold(ceilingDb);
    }

    void reset() noexcept { limiter.reset(); }

private:
    juce::dsp::Limiter<float> limiter;
    float ceilingDb = -0.3f;
};
