#include "SynthEngine.h"

SynthEngine::SynthEngine()
{
    addVoices();
    // Default sine wave sound — will be replaced by preset-driven configuration
    auto* sound = new juce::SynthesiserSound();
    // In practice, use a custom SynthSound subclass
}

void SynthEngine::addVoices()
{
    for (int i = 0; i < MAX_POLYPHONY; ++i)
        addVoice(new SynthVoice());
}

void SynthEngine::prepare(double sampleRate, int samplesPerBlock)
{
    setCurrentPlaybackSampleRate(sampleRate);
    for (int i = 0; i < getNumVoices(); ++i)
    {
        if (auto* voice = dynamic_cast<SynthVoice*>(getVoice(i)))
            voice->prepare(sampleRate, samplesPerBlock);
    }
}
