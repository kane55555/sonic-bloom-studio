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
    baseSampleRate    = sampleRate > 0 ? sampleRate : 44100.0;
    preparedBlockSize = juce::jmax(1, samplesPerBlock);

    // FX + layer bus always run at the host rate on the decimated bus.
    layerBus.prepare(baseSampleRate, preparedBlockSize);
    fx.prepare(baseSampleRate, preparedBlockSize);

    // (Re)build the oversamplers and prepare the voices at the effective rate.
    rebuildOversampling();
}

void SynthEngine::rebuildOversampling()
{
    const int    factor  = 1 << oversampleFactorLog2;
    const double effRate  = baseSampleRate * factor;
    const int    effBlock = preparedBlockSize * factor;

    // The JUCE Synthesiser + every voice must run at the oversampled rate so the
    // engines render anti-aliased; the result is decimated back to host rate.
    setCurrentPlaybackSampleRate(effRate);
    forEachSynthVoice([effRate, effBlock](SynthVoice& v)
    {
        v.prepare(effRate, effBlock);
    });

    if (oversampleFactorLog2 <= 0)
    {
        oversamplerDry.reset();
        oversamplerSend.reset();
        return;
    }

    auto makeOs = [this]() -> std::unique_ptr<juce::dsp::Oversampling<float>>
    {
        auto os = std::make_unique<juce::dsp::Oversampling<float>>(
            (size_t) 2,
            (size_t) oversampleFactorLog2,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            /*isMaximumQuality*/ true,
            /*useIntegerLatency*/ false);
        os->initProcessing((size_t) preparedBlockSize);
        os->reset();
        return os;
    };

    oversamplerDry  = makeOs();
    oversamplerSend = makeOs();
}

void SynthEngine::setOversamplingFactor(int factorLog2)
{
    factorLog2 = juce::jlimit(0, 2, factorLog2);
    if (factorLog2 == oversampleFactorLog2)
        return;

    // Re-build happens only on a user-driven change (not per audio block), so
    // the steady-state render path performs no allocations.
    const juce::ScopedLock sl(lock);
    oversampleFactorLog2 = factorLog2;
    rebuildOversampling();
}

void SynthEngine::renderBlockWithFx(juce::AudioBuffer<float>& buffer,
                                    const juce::MidiBuffer& midi,
                                    int startSample, int numSamples)
{
    dryRenderBuffer.setSize(buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
    fxSendBuffer.setSize(buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
    dryRenderBuffer.clear();
    fxSendBuffer.clear();

    const int factor = 1 << oversampleFactorLog2;
    const bool useOversampling = factor > 1
                                 && oversamplerDry != nullptr
                                 && oversamplerSend != nullptr
                                 && numSamples > 0
                                 && numSamples <= preparedBlockSize;

    if (! useOversampling)
    {
        // Legacy / bit-identical path: voices render straight at host rate.
        SynthVoice::beginFxSendRender(&fxSendBuffer);
        renderNextBlock(dryRenderBuffer, midi, startSample, numSamples);
        SynthVoice::endFxSendRender();
    }
    else
    {
        // Up-sample the (silent) host-rate blocks to obtain high-rate scratch
        // blocks owned by the oversamplers, render the voices into them at the
        // oversampled rate, then decimate back into the host-rate buffers.
        juce::dsp::AudioBlock<float> dryBase (dryRenderBuffer);
        juce::dsp::AudioBlock<float> sendBase(fxSendBuffer);
        auto drySub  = dryBase.getSubBlock ((size_t) startSample, (size_t) numSamples);
        auto sendSub = sendBase.getSubBlock((size_t) startSample, (size_t) numSamples);

        auto dryHiBlk  = oversamplerDry->processSamplesUp(drySub);    // silent, numSamples*factor
        auto sendHiBlk = oversamplerSend->processSamplesUp(sendSub);  // silent

        const int hiN = (int) dryHiBlk.getNumSamples();
        const int nch = juce::jmin((int) dryHiBlk.getNumChannels(), 8);
        float* dryPtrs [8] = { nullptr };
        float* sendPtrs[8] = { nullptr };
        for (int ch = 0; ch < nch; ++ch)
        {
            dryPtrs [ch] = dryHiBlk.getChannelPointer ((size_t) ch);
            sendPtrs[ch] = sendHiBlk.getChannelPointer((size_t) ch);
        }
        juce::AudioBuffer<float> dryHi (dryPtrs,  nch, hiN);
        juce::AudioBuffer<float> sendHi(sendPtrs, nch, hiN);

        // Scale MIDI timestamps into the oversampled timebase so note events
        // keep their position within the block.
        scaledMidi.clear();
        for (const auto meta : midi)
            scaledMidi.addEvent(meta.getMessage(),
                                (meta.samplePosition - startSample) * factor);

        SynthVoice::beginFxSendRender(&sendHi);
        renderNextBlock(dryHi, scaledMidi, 0, hiN);
        SynthVoice::endFxSendRender();

        // Decimate the high-rate synthesis back into the host-rate buffers.
        oversamplerDry->processSamplesDown(drySub);
        oversamplerSend->processSamplesDown(sendSub);
    }

    updateHeldNotes(midi);
    layerBus.process(dryRenderBuffer);
    // Task 6/7: capture the dry voice-bus peak BEFORE any FX return is mixed in
    // so the reporter can tell instrument silence apart from FX silence.
    fx.captureDryOutputPeak(dryRenderBuffer);
    fx.setActiveVoiceCountForDensity(getActiveVoiceCount());
    fx.processWetSend(fxSendBuffer);

    buffer.makeCopyOf(dryRenderBuffer, true);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.addFrom(ch, 0, fxSendBuffer, ch, 0, buffer.getNumSamples());
    fx.finalizeOutput(buffer);
}

void SynthEngine::resetForPresetChange()
{
    const juce::ScopedLock callbackLock(lock);
    chokeAllFxSends(50.0f);
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
    layerBus.reset();

    for (int channel = 0; channel < static_cast<int>(heldNotes.size()); ++channel)
        for (int note = 0; note < static_cast<int>(heldNotes[channel].size()); ++note)
            if (heldNotes[channel][note].active)
                noteOn(channel + 1, note, heldNotes[channel][note].velocity);
}

void SynthEngine::chokeAllFxSends(float fadeMs) noexcept
{
    forEachSynthVoice([fadeMs](SynthVoice& v)
    {
        v.chokeFxSend(fadeMs);
    });
}

void SynthEngine::setFxSendReleaseMsForAll(float ms) noexcept
{
    currentFxSendReleaseMs = juce::jlimit(1.0f, 500.0f, ms);
    forEachSynthVoice([this](SynthVoice& v)
    {
        v.setFxSendReleaseMs(currentFxSendReleaseMs);
    });
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

void SynthEngine::clearHeldNotes() noexcept
{
    for (auto& channel : heldNotes)
        for (auto& note : channel)
            note = {};
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
        v->setFallbackSynthesisEnabled(fallbackSynthesisEnabled);
        v->setFxSendReleaseMs(currentFxSendReleaseMs);
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

    if (instrumentName.isNotEmpty() && ms == nullptr)
        juce::Logger::writeToLog("[DIDITAGAIN sample] failed to load instrument folder: " + instrumentName);

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

    if (sourcePath.isNotEmpty() && ms == nullptr)
        juce::Logger::writeToLog("[DIDITAGAIN sample] failed to load imported source: " + sourcePath
            + " rootMidi=" + juce::String(rootMidi));

    activeMultisample = ms;
    currentInstrumentName = sourceName;

    forEachSynthVoice([&](SynthVoice& v)
    {
        v.setMultisample(activeMultisample);
    });

    return ms != nullptr || sourcePath.isEmpty();
}

bool SynthEngine::setMultisampleSources(const juce::Array<juce::File>& files,
                                        const juce::String& displayName)
{
    // Build a unique cache name so repeated calls with the same file set are
    // no-ops at the engine level (SampleLibrary also caches by content).
    juce::StringArray paths;
    for (auto& f : files) paths.add(f.getFullPathName());
    paths.sort(true);
    const auto sourceName = juce::String("multi:") + paths.joinIntoString("|");
    if (sourceName == currentInstrumentName && activeMultisample != nullptr)
    {
        juce::Logger::writeToLog("[DIDITAGAIN multisample] loaded multisample zones: "
            + juce::String((int) activeMultisample->zones.size()) + " name=" + displayName + " (reused)");
        return true;
    }

    auto ms = files.isEmpty()
        ? std::shared_ptr<const dida::Multisample>{}
        : dida::SampleLibrary::loadMultisampleFromFiles(files, displayName);

    if (! files.isEmpty() && ms == nullptr)
        juce::Logger::writeToLog("[DIDITAGAIN sample] failed to load multisample group: " + displayName
            + " files=" + juce::String(files.size()));
    else if (ms != nullptr)
        juce::Logger::writeToLog("[DIDITAGAIN multisample] loaded multisample zones: "
            + juce::String((int) ms->zones.size()) + " name=" + displayName);

    activeMultisample = ms;
    currentInstrumentName = sourceName;

    forEachSynthVoice([&](SynthVoice& v)
    {
        v.setMultisample(activeMultisample);
    });

    return ms != nullptr || files.isEmpty();
}

bool SynthEngine::loadMultisamplePreset(const juce::String& category,
                                        const juce::String& presetName,
                                        const juce::String& folderPath)
{
    juce::ignoreUnused(category, presetName);

    // A .diapreset changes processor parameters, but all guitar variants point
    // to the same base multisample folder. Keep the loaded source identity tied
    // to the folder path, not the cosmetic preset name, so preset changes reuse
    // Guitar 1 instead of clearing/reloading the sample map for each variant.
    const auto sourceName = juce::String("folder:") + folderPath;
    if (sourceName == currentInstrumentName && activeMultisample != nullptr)
    {
        juce::Logger::writeToLog("[DIDITAGAIN multisample] loaded multisample zones: "
            + juce::String((int) activeMultisample->zones.size()) + " name=" + presetName + " (reused)");
        return true;
    }

    auto ms = folderPath.isEmpty()
        ? std::shared_ptr<const dida::Multisample>{}
        : dida::SampleLibrary::loadMultisamplePreset(category, presetName, folderPath);

    if (folderPath.isNotEmpty() && ms == nullptr)
        juce::Logger::writeToLog("[DIDITAGAIN sample] failed to load multisample preset folder: "
            + category + " > " + presetName + " path=" + folderPath);
    else if (ms != nullptr)
        juce::Logger::writeToLog("[DIDITAGAIN multisample] loaded multisample zones: "
            + juce::String((int) ms->zones.size()) + " name=" + presetName);

    activeMultisample = ms;
    currentInstrumentName = sourceName;

    forEachSynthVoice([&](SynthVoice& v)
    {
        v.setMultisample(activeMultisample);
    });

    return ms != nullptr || folderPath.isEmpty();
}

void SynthEngine::setFallbackSynthesisEnabled(bool enabled)
{
    fallbackSynthesisEnabled = enabled;
    forEachSynthVoice([enabled](SynthVoice& v)
    {
        v.setFallbackSynthesisEnabled(enabled);
    });
}

void SynthEngine::setSampleLooping(bool shouldLoop)
{
    forEachSynthVoice([shouldLoop](SynthVoice& v)
    {
        v.setSampleLooping(shouldLoop);
    });
}

void SynthEngine::setSampleCropLoop(float cs, float ce, float ls, float le,
                                    float xfMs, bool oneShot, bool pitchTrack)
{
    forEachSynthVoice([&](SynthVoice& v)
    {
        v.setCropRange(cs, ce);
        v.setLoopRange(ls, le);
        v.setLoopCrossfadeMs(xfMs);
        v.setOneShotMode(oneShot);
        v.setPitchTracking(pitchTrack);
    });
}
