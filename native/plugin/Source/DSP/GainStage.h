#pragma once
//==============================================================================
//  GainStage.h — Smoothed master gain trim.
//==============================================================================
#include <JuceHeader.h>

class GainStage
{
public:
    void prepare(double sampleRate, int /*samplesPerBlock*/) noexcept
    {
        smoothed.reset(sampleRate, 0.02);
        smoothed.setCurrentAndTargetValue(targetGain);
    }

    void setGainDb(float db) noexcept
    {
        targetGain = juce::Decibels::decibelsToGain(juce::jlimit(-60.0f, 12.0f, db));
        smoothed.setTargetValue(targetGain);
    }

    // Current master-gain target as a linear multiplier. Used by FxChain to
    // meter the dry voice bus at the SAME output gain as the final buffer so
    // dryOutputPeakDb and finalOutputPeakDb are directly comparable.
    float getTargetGainLinear() const noexcept { return targetGain; }
    float getTargetGainDb()     const noexcept { return juce::Decibels::gainToDecibels(targetGain); }

    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();
        const int nc = buffer.getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            const float g = smoothed.getNextValue();
            for (int ch = 0; ch < nc; ++ch)
                buffer.getWritePointer(ch)[i] *= g;
        }
    }

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothed { 1.0f };
    float targetGain = 1.0f;
};
