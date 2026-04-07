#pragma once
#include <JuceHeader.h>

class FxChain
{
public:
    FxChain() = default;

    void prepare(double sampleRate, int samplesPerBlock)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<uint32_t>(samplesPerBlock);
        spec.numChannels = 2;

        chorus.prepare(spec);
        chorus.setRate(1.0f);
        chorus.setDepth(0.25f);
        chorus.setMix(0.0f);

        reverb.prepare(spec);
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);

        chorus.process(context);

        // Reverb
        juce::Reverb::Parameters reverbParams;
        reverbParams.roomSize = reverbSize;
        reverbParams.wetLevel = reverbMix;
        reverbParams.dryLevel = 1.0f - reverbMix;
        reverb.setParameters(reverbParams);
        reverb.processStereo(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
    }

    void setChorusMix(float mix) { chorus.setMix(mix); }
    void setReverbMix(float mix) { reverbMix = mix; }
    void setReverbSize(float size) { reverbSize = size; }

    void reset()
    {
        chorus.reset();
        reverb.reset();
    }

private:
    juce::dsp::Chorus<float> chorus;
    juce::Reverb reverb;

    float reverbMix = 0.0f;
    float reverbSize = 0.5f;
};
