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
    updateHeldNotes(midi);
    fx.process(buffer);
}

void SynthEngine::resetForPresetChange()
{
    const juce::ScopedLock lock(getLock());
    allNotesOff(0, false);
    const auto sampleRate = getSampleRate();
    forEachSynthVoice([](SynthVoice& v)
    {
        v.clearCurrentNote();
    });
    forEachSynthVoice([sampleRate](SynthVoice& v)
    {
        v.prepare(sampleRate, 0);
    });
    fx.reset();

    for (int channel = 0; channel < static_cast<int>(heldNotes.size()); ++channel)
        for (int note = 0; note < static_cast<int>(heldNotes[channel].size()); ++note)
            if (heldNotes[channel][note].active)
                noteOn(channel + 1, note, heldNotes[channel][note].velocity);
}

void SynthEngine::updateHeldNotes(const juce::MidiBuffer& midi)
{
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int channel = juce::jlimit(1, 16, msg.getChannel()) - 1;

        if (msg.isNoteOn())
        {
            const int note = juce::jlimit(0, 127, msg.getNoteNumber());
            heldNotes[channel][note].active = true;
            heldNotes[channel][note].velocity = msg.getFloatVelocity();
        }
        else if (msg.isNoteOff())
        {
            const int note = juce::jlimit(0, 127, msg.getNoteNumber());
            heldNotes[channel][note] = {};
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (auto& noteState : heldNotes[channel])
                noteState = {};
        }
    }
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

    for (int channel = 0; channel < static_cast<int>(heldNotes.size()); ++channel)
        for (int note = 0; note < static_cast<int>(heldNotes[channel].size()); ++note)
            if (heldNotes[channel][note].active)
                noteOn(channel + 1, note, heldNotes[channel][note].velocity);
}

void SynthEngine::setMonoMode(bool mono)
{
    if (mono == monoMode && getNumVoices() == (mono ? 1 : MAX_POLYPHONY))
        return;
    monoMode = mono;
    setNoteStealingEnabled(true);
    setMaxPolyphony(mono ? 1 : MAX_POLYPHONY);
}
