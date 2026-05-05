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

void SynthEngine::resetForPresetChange()
{
    allNotesOff(0, false);
    forEachSynthVoice([](SynthVoice& v)
    {
        v.clearCurrentNote();
        v.prepare(v.getSampleRate(), 0);
    });
}

void SynthEngine::setMaxPolyphony(int n)
{
    n = juce::jlimit(1, MAX_POLYPHONY, n);
    if (n == getNumVoices()) return;

    // Silence everything before mutating the voice list so we don't
    // delete a voice that's currently rendering audio.
    allNotesOff(0, false);
    for (int i = 0; i < getNumVoices(); ++i)
        if (auto* v = getVoice(i))
            v->clearCurrentNote();

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
    if (mono == monoMode && getNumVoices() == (mono ? 1 : MAX_POLYPHONY))
        return;
    monoMode = mono;
    setNoteStealingEnabled(true);
    setMaxPolyphony(mono ? 1 : MAX_POLYPHONY);
}
