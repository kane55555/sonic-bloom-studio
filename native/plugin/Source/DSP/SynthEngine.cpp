#include "SynthEngine.h"

SynthEngine::SynthEngine()
{
    for (int i = 0; i < MAX_POLYPHONY; ++i)
        addVoice(new SynthVoice());

    addSound(new DiditagainSynthSound());
    setNoteStealingEnabled(true);
}

void SynthEngine::prepare(double sampleRate, int samplesPerBlock)
{
    setCurrentPlaybackSampleRate(sampleRate);
    forEachSynthVoice([sampleRate, samplesPerBlock](SynthVoice& v)
    {
        v.prepare(sampleRate, samplesPerBlock);
    });
    fx.prepare(sampleRate, samplesPerBlock);
}

void SynthEngine::renderBlockWithFx(juce::AudioBuffer<float>& buffer,
                                    const juce::MidiBuffer& midi,
                                    int startSample, int numSamples)
{
    renderNextBlock(buffer, midi, startSample, numSamples);
    fx.process(buffer);
}

void SynthEngine::setMaxPolyphony(int n)
{
    n = juce::jlimit(1, MAX_POLYPHONY, n);
    while (getNumVoices() > n)
        removeVoice(getNumVoices() - 1);
    while (getNumVoices() < n)
    {
        auto* v = new SynthVoice();
        v->prepare(getSampleRate(), 0);
        addVoice(v);
    }
}

void SynthEngine::setMonoMode(bool mono)
{
    monoMode = mono;
    setNoteStealingEnabled(true);
    // In mono mode we cap voices to 1 to enforce monophonic behaviour.
    if (mono) setMaxPolyphony(1);
}
