#pragma once
//==============================================================================
//  EQBlock.h — 3-band shelving/peak EQ (low shelf, mid peak, high shelf).
//==============================================================================
#include <JuceHeader.h>

class EQBlock
{
public:
    void prepare(double sampleRate, int samplesPerBlock) noexcept
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate,
                                      static_cast<juce::uint32>(samplesPerBlock), 2 };
        chain.prepare(spec);
        update();
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        if (std::abs(lowDb) < 0.01f && std::abs(midDb) < 0.01f && std::abs(highDb) < 0.01f)
            return;
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        chain.process(ctx);
    }

    void setLowDb (float db) noexcept { lowDb  = juce::jlimit(-18.0f, 18.0f, db); update(); }
    void setMidDb (float db) noexcept { midDb  = juce::jlimit(-18.0f, 18.0f, db); update(); }
    void setHighDb(float db) noexcept { highDb = juce::jlimit(-18.0f, 18.0f, db); update(); }

    void reset() noexcept { chain.reset(); }

private:
    void update() noexcept
    {
        using Coeffs = juce::dsp::IIR::Coefficients<float>;
        *chain.get<0>().state = *Coeffs::makeLowShelf (sr, 200.0f,  0.7f, juce::Decibels::decibelsToGain(lowDb));
        *chain.get<1>().state = *Coeffs::makePeakFilter(sr, 1000.0f, 0.9f, juce::Decibels::decibelsToGain(midDb));
        *chain.get<2>().state = *Coeffs::makeHighShelf(sr, 6000.0f, 0.7f, juce::Decibels::decibelsToGain(highDb));
    }

    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                  juce::dsp::IIR::Coefficients<float>>;
    juce::dsp::ProcessorChain<Filter, Filter, Filter> chain;
    double sr = 44100.0;
    float lowDb = 0.0f, midDb = 0.0f, highDb = 0.0f;
};
