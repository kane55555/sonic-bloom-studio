#pragma once
//==============================================================================
//  ReverbBlock.h — Wrapper around juce::Reverb. Provides clean parameter
//  setters and ensures we never feed it invalid values.
//==============================================================================
#include <JuceHeader.h>

class ReverbBlock
{
public:
    void prepare(double sampleRate, int /*samplesPerBlock*/) noexcept
    {
        reverb.reset();
        reverb.setSampleRate(sampleRate);
        push();
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        if (mix <= 0.0001f) return;
        push();
        if (buffer.getNumChannels() >= 2)
        {
            reverb.processStereo(buffer.getWritePointer(0),
                                 buffer.getWritePointer(1),
                                 buffer.getNumSamples());
        }
        else
        {
            reverb.processMono(buffer.getWritePointer(0), buffer.getNumSamples());
        }
    }

    void setMix    (float m) noexcept { mix     = juce::jlimit(0.0f, 1.0f, m); }
    void setSize   (float s) noexcept { size    = juce::jlimit(0.0f, 1.0f, s); }
    void setDamping(float d) noexcept { damping = juce::jlimit(0.0f, 1.0f, d); }
    void setWidth  (float w) noexcept { width   = juce::jlimit(0.0f, 1.0f, w); }

    void reset() noexcept { reverb.reset(); }

private:
    void push() noexcept
    {
        juce::Reverb::Parameters p;
        p.roomSize   = size;
        p.damping    = damping;
        p.width      = width;
        p.wetLevel   = mix;
        p.dryLevel   = 1.0f - mix;
        p.freezeMode = 0.0f;
        reverb.setParameters(p);
    }

    juce::Reverb reverb;
    float mix = 0.0f, size = 0.5f, damping = 0.5f, width = 1.0f;
};
