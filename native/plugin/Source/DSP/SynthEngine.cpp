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
    const juce::ScopedLock callbackLock(lock);
    if (! canSafelyResetVoices())
        return;

    allNotesOff(0, false);
    const auto sampleRate = getSampleRate();
    forEachSynthVoice([](SynthVoice& v)
    {
        v.resetNote();
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

bool SynthEngine::hasHeldNotes() const noexcept
{
    return getHeldNoteCount() > 0;
}

bool SynthEngine::hasActiveVoices() const noexcept
{
    return getActiveVoiceCount() > 0;
}

int SynthEngine::getHeldNoteCount() const noexcept
{
    int count = 0;
    for (const auto& channel : heldNotes)
        for (const auto& note : channel)
            if (note.active)
                ++count;

    return count;
}

int SynthEngine::getActiveVoiceCount() const noexcept
{
    int count = 0;
    for (int i = 0; i < getNumVoices(); ++i)
        if (auto* voice = getVoice(i))
            if (voice->isVoiceActive())
                ++count;

    return count;
}

bool SynthEngine::canSafelyResetVoices() const noexcept
{
    return ! hasHeldNotes() && ! hasActiveVoices();
}

bool SynthEngine::canSafelyMutateVoices(const juce::MidiBuffer& upcomingMidi) const noexcept
{
    return canSafelyResetVoices() && ! midiBufferHasPhraseActivity(upcomingMidi);
}

bool SynthEngine::midiBufferHasPhraseActivity(const juce::MidiBuffer& midi) noexcept
{
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn() || msg.isNoteOff() || msg.isAllNotesOff() || msg.isAllSoundOff())
            return true;
    }

    return false;
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

bool SynthEngine::setMaxPolyphony(int n)
{
    const juce::ScopedLock callbackLock(lock);

    n = juce::jlimit(1, MAX_POLYPHONY, n);
    if (n == getNumVoices()) return true;

    if (! canSafelyResetVoices())
        return false;

    // Silence everything before mutating the voice list so we don't
    // delete a voice that's currently rendering audio.
    allNotesOff(0, false);
    forEachSynthVoice([](SynthVoice& v)
    {
        v.resetNote();
    });

    while (getNumVoices() > n)
        removeVoice(getNumVoices() - 1);
    while (getNumVoices() < n)
    {
        auto* v = new SynthVoice();
        v->prepare(getSampleRate(), 0);
        v->setMultisample(activeMultisample);
        addVoice(v);
    }

    for (int channel = 0; channel < static_cast<int>(heldNotes.size()); ++channel)
        for (int note = 0; note < static_cast<int>(heldNotes[channel].size()); ++note)
            if (heldNotes[channel][note].active)
                noteOn(channel + 1, note, heldNotes[channel][note].velocity);

    return true;
}

bool SynthEngine::setMonoMode(bool mono)
{
    const int targetVoices = mono ? 1 : getNumVoices();
    if (mono == monoMode && getNumVoices() == targetVoices)
        return true;

    if (! canSafelyResetVoices())
        return false;

    monoMode = mono;
    setNoteStealingEnabled(true);
    if (mono && ! setMaxPolyphony(1))
        return false;

    return true;
}

bool SynthEngine::setInstrument(const juce::String& instrumentName)
{
    if (instrumentName == currentInstrumentName && activeMultisample != nullptr)
        return true;

    auto ms = instrumentName.isEmpty()
        ? std::shared_ptr<const dida::Multisample>{}
        : dida::SampleLibrary::loadInstrument(instrumentName);

    activeMultisample = ms;
    currentInstrumentName = instrumentName;

    forEachSynthVoice([&](SynthVoice& v)
    {
        v.setMultisample(activeMultisample);
    });

    return ms != nullptr || instrumentName.isEmpty();
}

bool SynthEngine::setSampleSource(const juce::String& sourcePath, int rootMidi, const juce::String& displayName)
{
    const auto sourceName = sourcePath.isEmpty() ? juce::String()
        : (juce::String("sample:") + sourcePath + ":" + juce::String(rootMidi));
    if (sourceName == currentInstrumentName && activeMultisample != nullptr)
        return true;

    auto ms = sourcePath.isEmpty()
        ? std::shared_ptr<const dida::Multisample>{}
        : dida::SampleLibrary::loadSampleSource(sourcePath, rootMidi, displayName);

    activeMultisample = ms;
    currentInstrumentName = sourceName;

    forEachSynthVoice([&](SynthVoice& v)
    {
        v.setMultisample(activeMultisample);
    });

    return ms != nullptr || sourcePath.isEmpty();
}

void SynthEngine::setFallbackSynthesisEnabled(bool enabled)
{
    forEachSynthVoice([enabled](SynthVoice& v)
    {
        v.setFallbackSynthesisEnabled(enabled);
    });
}
