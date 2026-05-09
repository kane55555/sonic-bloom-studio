#pragma once
//==============================================================================
//  CompressorBlock.h — Stereo feed-forward compressor wrapping juce::dsp::Compressor.
//==============================================================================
#include <JuceHeader.h>

class CompressorBlock
{
public:
    void prepare(double sampleRate, int samplesPerBlock) noexcept
    {
        juce::dsp::ProcessSpec spec { sampleRate,
                                      static_cast<juce::uint32>(samplesPerBlock), 2 };
        comp.prepare(spec);
        comp.setAttack(8.0f);
        comp.setRelease(120.0f);
        push();
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        if (! enabled) return;
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        comp.process(ctx);
    }

    void setThresholdDb(float db) noexcept { threshold = juce::jlimit(-60.0f, 0.0f, db); push(); }
    void setRatio      (float r ) noexcept { ratio     = juce::jlimit(1.0f, 20.0f, r); push(); }
    void setEnabled    (bool e  ) noexcept { enabled = e; }

    void reset() noexcept { comp.reset(); }

private:
    void push() noexcept
    {
        comp.setThreshold(threshold);
        comp.setRatio(ratio);
    }

    juce::dsp::Compressor<float> comp;
    float threshold = -12.0f;
    float ratio     = 2.0f;
    bool  enabled   = false;
};
