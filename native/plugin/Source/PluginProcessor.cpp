#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets/PresetQualityReport.h"

static inline void didaPresetLog(const juce::String& message)
{
    DBG(juce::String("[DIDITAGAIN PRESET] ") + message);
}

DiditagainProcessor::DiditagainProcessor()
    : AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      synthEngine(),
      presetManager(*this),
      licenseClient()
{
    {
        const bool clearOnStartup =
            apvts.state.getProperty("clearPresetLogOnStartup", false);
        dida::presetreport::initSession(clearOnStartup);
    }

    presetManager.onPresetLoaded = [this]()
    {
        presetLoadRequested.store(true, std::memory_order_release);
        presetLoadSerial.fetch_add(1, std::memory_order_acq_rel);
        didaPresetLog(juce::String("load requested serial=") + juce::String(presetLoadSerial.load(std::memory_order_acquire))
            + " index=" + juce::String(presetManager.getCurrentPresetIndex())
            + " name=" + presetManager.getPresetName(presetManager.getCurrentPresetIndex()));
    };
}

DiditagainProcessor::~DiditagainProcessor() {}

static inline void didaAudioLog(const juce::String& message)
{
    DBG(juce::String("[DIDITAGAIN AUDIO] ") + message);
}

juce::AudioProcessorValueTreeState::ParameterLayout DiditagainProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Master — a SMALL final trim only (Report 78). Default 0 dB, clamped to a
    // safe ±6 dB range. Per-preset loudness lives in amp.gainDb (the ampGain
    // stage), never here, so master can no longer collapse/overboost the bus.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"masterGain", 1}, "Master Gain",
        juce::NormalisableRange<float>(-6.0f, 6.0f, 0.1f), 0.0f));

    // Amp gain — the PRIMARY per-preset loudness calibration (amp.gainDb).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"ampGain", 1}, "Amp Gain",
        juce::NormalisableRange<float>(-60.0f, 24.0f, 0.1f), 0.0f));

    // Engine
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"engineMode", 1}, "Engine Mode",
        juce::StringArray{"Subtractive", "FM2", "FM4", "Wavetable", "Layered"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"polyphony", 1}, "Polyphony", 1, 16, 8));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"monoMode", 1}, "Mono Mode", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"fmAmount", 1}, "FM Amount",
        juce::NormalisableRange<float>(0.0f, 12.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"fmRatio", 1}, "FM Ratio",
        juce::NormalisableRange<float>(0.25f, 16.0f, 0.01f, 0.4f), 1.0f));

    // Oscillator A
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"oscAWaveform", 1}, "Osc A Waveform",
        juce::StringArray{"Sine", "Triangle", "Saw", "Square", "Pulse", "SuperSaw", "FmCarrier", "Wavetable"}, 2));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"oscALevel", 1}, "Osc A Level",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"oscADetune", 1}, "Osc A Detune",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"oscAOctave", 1}, "Osc A Octave", -3, 3, 0));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"oscASemi", 1}, "Osc A Semi", -12, 12, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"oscAPulseWidth", 1}, "Osc A Pulse Width",
        juce::NormalisableRange<float>(0.01f, 0.99f), 0.5f));

    // Oscillator B
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"oscBWaveform", 1}, "Osc B Waveform",
        juce::StringArray{"Sine", "Triangle", "Saw", "Square", "Pulse", "SuperSaw", "FmCarrier", "Wavetable"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"oscBLevel", 1}, "Osc B Level",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"oscBDetune", 1}, "Osc B Detune",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"oscBOctave", 1}, "Osc B Octave", -3, 3, 0));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"oscBSemi", 1}, "Osc B Semi", -12, 12, 0));

    // Sub Oscillator
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"subOscEnabled", 1}, "Sub Osc Enabled", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"subOscLevel", 1}, "Sub Osc Level",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    // Noise
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"noiseLevel", 1}, "Noise Level",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"noiseType", 1}, "Noise Type",
        juce::StringArray{"White", "Pink"}, 0));

    // Filter 1
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filter1Cutoff", 1}, "Filter 1 Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 8000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filter1Resonance", 1}, "Filter 1 Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"filter1Type", 1}, "Filter 1 Type",
        juce::StringArray{"LP12", "LP24", "HP12", "HP24", "BP", "Notch"}, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filter1Drive", 1}, "Filter 1 Drive",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filter1EnvAmount", 1}, "Filter 1 Env Amount",
        juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filter1KeyTrack", 1}, "Filter 1 Key Track",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Envelopes (Amp, Filter, Mod)
    for (int i = 1; i <= 3; ++i)
    {
        juce::String prefix = "env" + juce::String(i);
        juce::String name = (i == 1) ? "Amp" : (i == 2) ? "Filter" : "Mod";
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{prefix + "Attack", 1}, name + " Attack",
            juce::NormalisableRange<float>(0.001f, 10.0f, 0.001f, 0.3f), 0.01f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{prefix + "Decay", 1}, name + " Decay",
            juce::NormalisableRange<float>(0.001f, 10.0f, 0.001f, 0.3f), 0.3f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{prefix + "Sustain", 1}, name + " Sustain",
            juce::NormalisableRange<float>(0.0f, 1.0f), 0.7f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{prefix + "Release", 1}, name + " Release",
            juce::NormalisableRange<float>(0.001f, 15.0f, 0.001f, 0.3f), 0.5f));
    }

    // LFOs
    for (int i = 1; i <= 2; ++i)
    {
        juce::String prefix = "lfo" + juce::String(i);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{prefix + "Rate", 1}, "LFO " + juce::String(i) + " Rate",
            juce::NormalisableRange<float>(0.01f, 30.0f, 0.01f, 0.4f), 1.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{prefix + "Shape", 1}, "LFO " + juce::String(i) + " Shape",
            juce::StringArray{"Sine", "Triangle", "Saw", "Square", "S&H"}, 0));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{prefix + "Sync", 1}, "LFO " + juce::String(i) + " Sync", false));
    }

    // Unison
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"unisonVoices", 1}, "Unison Voices", 1, 8, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"unisonDetune", 1}, "Unison Detune",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"unisonSpread", 1}, "Unison Spread",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    // Glide
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"glideTime", 1}, "Glide Time",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f), 0.0f));

    // Macros 1-8
    for (int i = 1; i <= 8; ++i)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"macro" + juce::String(i), 1},
            "Macro " + juce::String(i),
            juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    }

    // Effects
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"fxChorusMix", 1}, "Chorus Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"fxDelayMix", 1}, "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"fxDelayTime", 1}, "Delay Time",
        juce::NormalisableRange<float>(0.01f, 2.0f, 0.01f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"fxDelayFeedback", 1}, "Delay Feedback",
        juce::NormalisableRange<float>(0.0f, 0.95f), 0.4f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"fxReverbMix", 1}, "Reverb Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"fxReverbSize", 1}, "Reverb Size",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"fxDistortionAmount", 1}, "Distortion",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    // Real saturation wet/dry mix — was previously forced to 100% whenever
    // drive was nonzero, turning subtle warmth into full distortion.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"fxSaturationMix", 1}, "Saturation Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.15f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"fxPhaserMix", 1}, "Phaser Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Quality mode
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"qualityMode", 1}, "Quality Mode",
        juce::StringArray{"Draft", "Standard", "High"}, 1));

    // EQ
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"eqLow",  1}, "EQ Low",
        juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"eqMid",  1}, "EQ Mid",
        juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"eqHigh", 1}, "EQ High",
        juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f));

    // Compressor
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"compEnabled", 1}, "Comp Enabled", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"compThreshold", 1}, "Comp Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -12.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"compRatio", 1}, "Comp Ratio",
        juce::NormalisableRange<float>(1.0f, 20.0f, 0.1f), 2.0f));

    // Limiter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"limiterCeiling", 1}, "Limiter Ceiling",
        juce::NormalisableRange<float>(-12.0f, 0.0f, 0.1f), -0.3f));

    // Wet FX HPF
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"fxWetHighPass", 1}, "Wet FX HPF",
        juce::NormalisableRange<float>(20.0f, 800.0f, 1.0f, 0.4f), 80.0f));

    // Direct Monitor — bypass reverb + delay for low-latency tracking.
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"directMonitor", 1}, "Direct Monitor", false));

    // Vintage analog amount — scales per-voice card calibration offsets
    // (pitch, gain, cutoff, pan, drift). 0 = clean/modern, 1 = unstable/vintage.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"vintageAmount", 1}, "Vintage Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.25f));

    // BBD chorus mode: I (slow/deep), II (fast/shallow), I+II (combined wide).
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"chorusMode", 1}, "Chorus Mode",
        juce::StringArray{"I", "II", "I+II"}, 0));


    // AI Texture v0.1 — live, global control over the cached neural texture
    // layer. These do not create textures; they only scale the neural texture
    // partials a preset already loaded. enabled=false or amount=0 fully mutes
    // the texture, leaving the main sample/synth untouched. Presets without a
    // neural texture partial are unaffected (sound identical to before).
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"aiTextureEnabled", 1}, "AI Texture", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"aiTextureAmount", 1}, "Texture Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    // Global oversampling quality (anti-aliasing for the synthesis engines).
    // Off is bit-identical to the legacy render path; 2x/4x reduce oscillator
    // aliasing at the cost of CPU + a little latency.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"oversample", 1}, "Oversampling",
        juce::StringArray{"Off", "2x", "4x"}, 0));

    return { params.begin(), params.end() };
}

void DiditagainProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synthEngine.prepare(sampleRate, samplesPerBlock);
    // Ensure MIDI produces sound on a fresh instance before any preset/sample
    // has been loaded — this is the "test tone" fallback path. The startup
    // default below replaces it with a real piano preset when one exists.
    // Only force the fallback on the very first prepare so a later re-prepare
    // (e.g. sample-rate change) never clobbers an already-loaded instrument.
    if (! startupDefaultApplied && ! hasRestoredState)
        synthEngine.setFallbackSynthesisEnabled(true);
    didaAudioLog(juce::String("prepareToPlay sampleRate=") + juce::String(sampleRate)
        + " blockSize=" + juce::String(samplesPerBlock)
        + " voices=" + juce::String(synthEngine.getNumVoices()));

    // Fresh-instance default sound / DAW project recall. Safe to call here —
    // the preset library scanned in the PresetManager constructor. This will
    // not override a restored project (guarded by hasRestoredState).
    applyStartupDefaultIfNeeded();
}

void DiditagainProcessor::applyStartupDefaultIfNeeded()
{
    // 1) Highest priority: replay a selection restored from DAW project state.
    if (restoredSelection.pending)
    {
        restoredSelection.pending = false;
        startupDefaultApplied = true;

        // Rescan so user/imported presets created since save are visible, then
        // resolve the saved selection by stable identity.
        presetManager.scanPresetDirectory();
        const int idx = presetManager.findPresetIndexByIdentity(
            restoredSelection.userPresetFilePath,
            restoredSelection.presetFilePath,
            restoredSelection.presetName,
            restoredSelection.presetCategory,
            restoredSelection.presetIndex);

        if (idx >= 0)
        {
            // Queue the source-folder load exactly like a normal preset click.
            presetManager.loadPreset(idx);
            didaPresetLog(juce::String("restored project selection idx=") + juce::String(idx)
                + " name=" + presetManager.getPresetName(idx));
        }
        else
        {
            synthEngine.setFallbackSynthesisEnabled(restoredSelection.fallbackSynthesisEnabled);
            didaPresetLog("restore: saved preset not found, kept fallback="
                + juce::String(restoredSelection.fallbackSynthesisEnabled ? "true" : "false"));
        }
        return;
    }

    // 2) Only auto-load the default for a truly new plugin instance.
    if (hasRestoredState || startupDefaultApplied) return;
    if (presetManager.getNumPresets() <= 0) return;

    startupDefaultApplied = true;
    const int idx = presetManager.findDefaultPianoPresetIndex();
    if (idx >= 0)
    {
        // Fallback synthesis is disabled automatically by processBlock once the
        // real sample source from this preset becomes active.
        presetManager.loadPreset(idx);
        didaPresetLog(juce::String("startup default piano idx=") + juce::String(idx)
            + " name=" + presetManager.getPresetName(idx));
    }
    else
    {
        synthEngine.setFallbackSynthesisEnabled(true);
        didaPresetLog("startup default: no piano preset found, using fallback synth");
    }
}

void DiditagainProcessor::releaseResources()
{
    synthEngine.allNotesOff(0, false);
}

bool DiditagainProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DiditagainProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // ---- Push current parameter snapshot to the engine ----
    auto getF = [this](const char* id) { return apvts.getRawParameterValue(id)->load(); };

    const bool mono     = getF("monoMode") > 0.5f;
    const int  polyWant = juce::jlimit(1, 16, static_cast<int>(getF("polyphony")));

    // Apply oversampling selection (Off/2x/4x -> log2 factor 0/1/2). Rebuild
    // only happens when the user changes it, never per-block in steady state.
    const int osLog2 = juce::jlimit(0, 2, static_cast<int>(getF("oversample")));
    if (osLog2 != synthEngine.getOversamplingFactorLog2())
        synthEngine.setOversamplingFactor(osLog2);

    const int latestPresetSerial = presetLoadSerial.load(std::memory_order_acquire);
    const bool newPresetLoaded = presetLoadRequested.exchange(false, std::memory_order_acq_rel)
        || latestPresetSerial != observedPresetLoadSerial;

    const bool queuedTargetChanged = deferredPresetChange.queued
        && (mono != deferredPresetChange.monoMode || polyWant != deferredPresetChange.polyphony);
    const bool appliedVoiceStateMismatch = ! deferredPresetChange.queued
        && (mono != appliedMonoMode || (! mono && polyWant != appliedPolyphony));

    if (newPresetLoaded || queuedTargetChanged || appliedVoiceStateMismatch)
    {
        if (newPresetLoaded)
        {
            observedPresetLoadSerial = latestPresetSerial;
            currentPresetLoadIdForReport = latestPresetSerial;
            lastRenderedPresetLoadId = 0;
            lastRenderedPresetName = {};
            lastRenderedPresetAmpGainDb = 0.0f;
            lastRenderTimestampMs = 0;
            blocksRenderedSincePresetLoad = 0;
            notesRenderedSincePresetLoad = 0;
        }

        const auto previousAge = deferredPresetChange.queued ? deferredPresetChange.ageInBlocks : 0;
        deferredPresetChange.queued = true;
        deferredPresetChange.resetState = true;
        deferredPresetChange.monoMode = mono;
        deferredPresetChange.polyphony = polyWant;
        deferredPresetChange.presetSerial = latestPresetSerial;
        deferredPresetChange.ageInBlocks = previousAge;

        didaPresetLog(juce::String("queued serial=") + juce::String(deferredPresetChange.presetSerial)
            + " mono=" + (deferredPresetChange.monoMode ? "true" : "false")
            + " poly=" + juce::String(deferredPresetChange.polyphony)
            + " appliedMono=" + (appliedMonoMode ? "true" : "false")
            + " appliedPoly=" + juce::String(appliedPolyphony)
            + " midiEvents=" + juce::String(midiMessages.getNumEvents()));
    }

    // Preset stepping may change mono/poly and requested polyphony every click.
    // Keep the latest request queued, but only touch JUCE's voice pool once the
    // host has stopped sending note events and all voices have fully released.
    // If we've waited too long (FL is actively playing a clip with held notes),
    // force-apply: silence everything, mutate, then let held notes retrigger.
    static constexpr int kForceApplyAfterBlocks = 16;

    if (deferredPresetChange.queued)
    {
        ++deferredPresetChange.ageInBlocks;
        const bool canApplyVoiceMutation = synthEngine.canSafelyMutateVoices(midiMessages);
        const bool voicePoolNeedsMutation = deferredPresetChange.monoMode != appliedMonoMode
            || (! deferredPresetChange.monoMode && deferredPresetChange.polyphony != appliedPolyphony);
        const bool sameVoicePool = ! voicePoolNeedsMutation;
        const bool forceApply = deferredPresetChange.ageInBlocks >= kForceApplyAfterBlocks;

        if (forceApply && ! canApplyVoiceMutation)
        {
            // Hard-stop everything so mutation/reset is safe.
            synthEngine.allNotesOff(0, false);
            synthEngine.forEachSynthVoice([](SynthVoice& v) { v.resetNote(); });
            didaPresetLog(juce::String("force-apply silencing voices serial=") + juce::String(deferredPresetChange.presetSerial)
                + " waitedBlocks=" + juce::String(deferredPresetChange.ageInBlocks));
        }

        if (canApplyVoiceMutation || sameVoicePool || forceApply)
        {
            bool applied = true;

            if (voicePoolNeedsMutation && deferredPresetChange.monoMode != appliedMonoMode)
            {
                applied = synthEngine.setMonoMode(deferredPresetChange.monoMode);
                if (applied)
                {
                    appliedMonoMode = deferredPresetChange.monoMode;
                    appliedPolyphony = synthEngine.getNumVoices();
                }
            }

            if (applied && voicePoolNeedsMutation && ! deferredPresetChange.monoMode && deferredPresetChange.polyphony != appliedPolyphony)
            {
                applied = synthEngine.setMaxPolyphony(deferredPresetChange.polyphony);
                if (applied)
                    appliedPolyphony = deferredPresetChange.polyphony;
            }

            // Sample-folder swap must happen regardless of whether the voice
            // pool mutation succeeded — otherwise a failed setMaxPolyphony
            // would leave the engine pointed at the previous preset's samples
            // and the new preset would be silent.
            {
                if (applied && deferredPresetChange.resetState && (canApplyVoiceMutation || forceApply))
                    synthEngine.resetForPresetChange();

                // Swap the active multisample instrument requested by the preset.
                const auto& requestedSample = presetManager.getRequestedSampleSource();
                const auto& requestedSampleList = presetManager.getRequestedSampleSources();
                const auto& requestedFolder = presetManager.getRequestedSampleFolderPath();
                const auto& requested = presetManager.getRequestedInstrument();
                synthEngine.setFallbackSynthesisEnabled(requestedSample.isEmpty()
                    && requestedSampleList.isEmpty()
                    && requestedFolder.isEmpty());
                bool sampleLoaded = false;
                if (requestedSampleList.size() > 1)
                {
                    juce::Array<juce::File> files;
                    for (auto& p : requestedSampleList) files.add(juce::File(p));
                    sampleLoaded = synthEngine.setMultisampleSources(files,
                                                     presetManager.getRequestedSampleDisplayName());
                    synthEngine.setSampleLooping(presetManager.getRequestedSampleLooping());
                    if (! sampleLoaded)
                        didaAudioLog(juce::String("multisample file-list load FAILED files=") + juce::String(files.size())
                            + " name=" + presetManager.getRequestedSampleDisplayName()
                            + " -> falling back to folder scan");
                }
                if (! sampleLoaded && requestedFolder.isNotEmpty())
                {
                    sampleLoaded = synthEngine.loadMultisamplePreset(presetManager.getRequestedCategory(),
                                                                     presetManager.getRequestedSampleDisplayName(),
                                                                     requestedFolder);
                    synthEngine.setSampleLooping(presetManager.getRequestedSampleLooping());
                    if (! sampleLoaded)
                        didaAudioLog("multisample folder load FAILED path=" + requestedFolder
                            + " name=" + presetManager.getRequestedSampleDisplayName()
                            + " -> falling back to single-sample path");
                }
                if (! sampleLoaded)
                {
                    if (requestedSample.isNotEmpty())
                    {
                        synthEngine.setSampleSource(requestedSample,
                                                    presetManager.getRequestedSampleRootMidi(),
                                                    presetManager.getRequestedSampleDisplayName());
                        synthEngine.setSampleLooping(presetManager.getRequestedSampleLooping());
                    }
                    else if (requested.isNotEmpty() && requested != synthEngine.getInstrumentName())
                    {
                        synthEngine.setInstrument(requested);
                        synthEngine.setSampleLooping(false);
                    }
                    else if (requested.isEmpty()
                             && requestedSampleList.isEmpty()
                              && requestedFolder.isEmpty()
                             && synthEngine.getInstrumentName().isNotEmpty())
                    {
                        synthEngine.setInstrument({});
                        synthEngine.setSampleLooping(false);
                    }
                }

                // .diapreset variants must win over the sample-drop defaults:
                // source first, then amp/filter/layer/FX/modulation values.
                presetManager.applyPendingUserDiapresetAfterSampleLoad();

                didaPresetLog(juce::String("applied serial=") + juce::String(deferredPresetChange.presetSerial)
                    + " mono=" + (deferredPresetChange.monoMode ? "true" : "false")
                    + " poly=" + juce::String(deferredPresetChange.polyphony)
                    + " mutatedVoices=" + (voicePoolNeedsMutation ? "true" : "false")
                    + " voiceMutationApplied=" + (applied ? "true" : "false")
                    + " forced=" + (forceApply ? "true" : "false")
                    + " waitedBlocks=" + juce::String(deferredPresetChange.ageInBlocks)
                    + " instrument=" + synthEngine.getInstrumentName());

                deferredPresetChange = {};
            }
        }
        else if ((++debugBlockCounter % 128) == 0)
        {
            didaPresetLog(juce::String("waiting serial=") + juce::String(deferredPresetChange.presetSerial)
                + " blocks=" + juce::String(deferredPresetChange.ageInBlocks)
                + " heldNotes=" + juce::String(synthEngine.getHeldNoteCount())
                + " activeVoices=" + juce::String(synthEngine.getActiveVoiceCount())
                + " midiEvents=" + juce::String(midiMessages.getNumEvents()));
        }
    }

    const auto engineMode = static_cast<SynthVoice::EngineMode>(
        static_cast<int>(getF("engineMode")));
    const float fmAmount = getF("fmAmount");
    const float fmRatio  = getF("fmRatio");
    const float oscALevel = getF("oscALevel");
    const float oscBLevel = getF("oscBLevel");
    const int   oscAOct   = static_cast<int>(getF("oscAOctave"));
    const int   oscASemi  = static_cast<int>(getF("oscASemi"));
    const int   oscBOct   = static_cast<int>(getF("oscBOctave"));
    const int   oscBSemi  = static_cast<int>(getF("oscBSemi"));
    const float subLevel  = getF("subOscEnabled") > 0.5f ? getF("subOscLevel") : 0.0f;
    const float noiseLvl  = getF("noiseLevel");
    const int   noiseType = static_cast<int>(getF("noiseType"));
    const float glide     = getF("glideTime");
    const float cutoff    = getF("filter1Cutoff");
    const float reso      = getF("filter1Resonance");
    const auto  filterType = static_cast<FilterBlock::Type>(static_cast<int>(getF("filter1Type")));
    const float fDrive    = getF("filter1Drive");
    const float fEnvAmt   = getF("filter1EnvAmount");
    const float keyTrk    = getF("filter1KeyTrack");

    // Envelopes
    const float ampA = getF("env1Attack"),  ampD = getF("env1Decay"),
                ampS = getF("env1Sustain"), ampR = getF("env1Release");
    const float fA = getF("env2Attack"),  fD = getF("env2Decay"),
                fS = getF("env2Sustain"), fR = getF("env2Release");
    const float mA = getF("env3Attack"),  mD = getF("env3Decay"),
                mS = getF("env3Sustain"), mR = getF("env3Release");

    const int   unisonVoices = juce::jlimit(1, 8, static_cast<int>(getF("unisonVoices")));
    const float unisonDetune = getF("unisonDetune");
    const float unisonSpread = getF("unisonSpread");
    const float vintageAmt   = juce::jlimit(0.0f, 1.0f, getF("vintageAmount"));

    int voiceCardCounter = 0;
    synthEngine.forEachSynthVoice([&](SynthVoice& v)
    {
        // Assign persistent voice card index round-robin so each polyphonic
        // voice has its own slight analog character (pitch, pan, drift, etc).
        v.setVoiceCardIndex(voiceCardCounter++);
        v.setVintageAmount(vintageAmt);

        v.setEngineMode(engineMode);
        v.setOscALevel(oscALevel);
        v.setOscBLevel(oscBLevel);
        v.setSubLevel(subLevel);
        v.setNoiseLevel(noiseLvl);
        v.setNoiseType(noiseType);
        v.setFmAmount(fmAmount);
        v.setFmRatio(fmRatio);
        v.setGlideSeconds(glide);
        v.setUnison(unisonVoices, unisonDetune, unisonSpread);
        v.setBaseCutoff(cutoff);
        v.setFilterEnvAmount(fEnvAmt);
        v.setFilterKeyTrack(keyTrk);

        auto& f = v.getFilter();
        f.setType(filterType);
        f.setResonance(reso);
        f.setDrive(fDrive);
        // Subtle post-filter saturation scales with the Vintage amount so
        // resonance peaks stay musical instead of digital/piercing.
        f.setOutputDrive(0.15f + 0.35f * vintageAmt);

        v.getOscA().setWaveform(static_cast<Oscillator::Waveform>(static_cast<int>(getF("oscAWaveform"))));
        v.getOscB().setWaveform(static_cast<Oscillator::Waveform>(static_cast<int>(getF("oscBWaveform"))));
        v.getOscA().setDetuneCents(getF("oscADetune"));
        v.getOscB().setDetuneCents(getF("oscBDetune"));
        v.getOscA().setPulseWidth(getF("oscAPulseWidth"));
        v.setOscAPitchOffset(oscAOct * 12 + oscASemi);
        v.setOscBPitchOffset(oscBOct * 12 + oscBSemi);

        v.getAmpEnv().setAttack(ampA);    v.getAmpEnv().setDecay(ampD);
        v.getAmpEnv().setSustain(ampS);   v.getAmpEnv().setRelease(ampR);
        v.getFilterEnv().setAttack(fA);   v.getFilterEnv().setDecay(fD);
        v.getFilterEnv().setSustain(fS);  v.getFilterEnv().setRelease(fR);
        v.getModEnv().setAttack(mA);      v.getModEnv().setDecay(mD);
        v.getModEnv().setSustain(mS);     v.getModEnv().setRelease(mR);
    });


    // ---- Preset-driven macro mapping (V2 macro targets). When a preset
    // declares macro targets, push macroN values to those APVTS params and
    // skip the legacy hardcoded macro behavior so we don't double-modulate. ----
    auto& macroMapper = presetManager.getMacroMapper();
    const bool presetMacrosActive = ! macroMapper.isEmpty();
    macroMapper.apply(*this);

    const float m1 = getF("macro1");
    const float m2 = getF("macro2");
    const float m3 = getF("macro3");
    const float m4 = getF("macro4");
    const float m5 = getF("macro5");
    const float m6 = getF("macro6");
    const float m7 = getF("macro7");
    const float m8 = getF("macro8");

    // ---- FX parameters (with macro modulation, all clamped 0..1) ----
    auto clamp01 = [](float v) { return juce::jlimit(0.0f, 1.0f, v); };
    auto& fx = synthEngine.getFx();

    const float driveAmt   = presetMacrosActive ? clamp01(getF("fxDistortionAmount"))
                                                : clamp01(getF("fxDistortionAmount") + m6 * 0.6f);
    const float satMixAmt  = clamp01(getF("fxSaturationMix"));
    fx.setSaturationDrive(driveAmt);
    // Use the real saturation mix parameter instead of forcing wet=1.0.
    // Saturation becomes inaudible only when drive *or* mix is zero.
    fx.setSaturationMix  (driveAmt > 0.0001f ? satMixAmt : 0.0f);

    const float chorusMix  = presetMacrosActive ? clamp01(getF("fxChorusMix"))
                                                : clamp01(getF("fxChorusMix") + m2 * 0.5f);
    fx.setChorusMix  (chorusMix);
    fx.setChorusRate (presetMacrosActive ? 0.6f : (0.4f + m2 * 1.6f));
    fx.setChorusDepth(juce::jlimit(0.05f, 1.0f, presetMacrosActive ? 0.35f : 0.25f + m8 * 0.5f));

    const bool directMonitor = getF("directMonitor") > 0.5f;

    fx.setDelayMix(directMonitor ? 0.0f : getF("fxDelayMix"));
    fx.setDelayTime(getF("fxDelayTime"));
    fx.setDelayFeedback(getF("fxDelayFeedback"));

    const float reverbMix = directMonitor ? 0.0f
        : (presetMacrosActive ? clamp01(getF("fxReverbMix"))
                              : clamp01(getF("fxReverbMix") + m7 * 0.25f));
    const float reverbSize = presetMacrosActive ? clamp01(getF("fxReverbSize"))
                                                : clamp01(getF("fxReverbSize") + m5 * 0.20f);
    fx.setReverbMix (juce::jlimit(0.0f, 0.45f, reverbMix));
    fx.setReverbSize(juce::jlimit(0.0f, 0.78f, reverbSize));

    fx.setChorusMode(static_cast<int>(getF("chorusMode")));
    fx.setEqLowDb (getF("eqLow"));

    fx.setEqMidDb (getF("eqMid"));
    fx.setEqHighDb(getF("eqHigh"));
    fx.setCompEnabled(getF("compEnabled") > 0.5f);
    fx.setCompThresholdDb(getF("compThreshold"));
    fx.setCompRatio(getF("compRatio"));
    fx.setLimiterCeilingDb(getF("limiterCeiling"));
    fx.setWetHighPassHz(getF("fxWetHighPass"));
    fx.setReverbInputHighPassFloorHz(getF("fxWetHighPass"));
    fx.setMasterGainDb(getF("masterGain"));
    fx.setAmpGainDb(getF("ampGain"));

    if (! presetMacrosActive)
    {
        // Legacy hardcoded macros — only when preset has no V2 macro targets.
        const float macroCutoff = std::pow(2.0f, (m1 - 0.5f) * 4.0f);
        const float macroOscB   = clamp01(oscBLevel + m3 * 0.7f);
        const float macroNoise  = clamp01(noiseLvl  + m4 * 0.5f);
        synthEngine.forEachSynthVoice([&](SynthVoice& v)
        {
            v.setBaseCutoff(juce::jlimit(20.0f, 20000.0f, cutoff * macroCutoff));
            v.setOscBLevel(macroOscB);
            v.setNoiseLevel(macroNoise);
        });
    }

    // ---- Debug: trace incoming MIDI note-ons ----
   #if JUCE_DEBUG
    for (const auto meta : midiMessages)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            didaAudioLog(juce::String("noteOn ch=") + juce::String(m.getChannel())
                + " note=" + juce::String(m.getNoteNumber())
                + " vel=" + juce::String(m.getFloatVelocity(), 2)
                + " instrument=" + (synthEngine.getInstrumentName().isEmpty()
                                        ? juce::String("<none/fallback>")
                                        : synthEngine.getInstrumentName()));
    }
   #endif

    // ---- Transport-stop watchdog: when the host stops playback, flush the
    // FX tails (reverb/delay) after a short grace period so a long preset
    // doesn't ring forever while the user has hit "pause". We do NOT reset
    // if any voices are still releasing — that would clip a held note.
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            const bool isPlaying = pos->getIsPlaying();
            if (! isPlaying)
            {
                stoppedBlocks++;
                const double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
                const int blockSamples = juce::jmax(1, buffer.getNumSamples());
                const int blocksFor120ms = juce::jmax(1, (int) (0.12 * sr / blockSamples));
                const int blocksFor700ms = juce::jmax(blocksFor120ms + 1, (int) (0.70 * sr / blockSamples));

                if (stoppedBlocks >= blocksFor120ms)
                    synthEngine.getFx().notifyTransportStopped(blockSamples);

                if (stoppedBlocks == blocksFor700ms)
                {
                    synthEngine.allNotesOff(0, false);
                    synthEngine.clearHeldNotes();
                    synthEngine.forEachSynthVoice([](SynthVoice& v) { v.resetNote(); });
                    synthEngine.getFx().reset();
                    didaAudioLog("transport stopped — killed voices and flushed FX tails");
                }
            }
            else
            {
                stoppedBlocks = 0;
                synthEngine.getFx().notifyTransportPlaying();
            }
        }
    }

    // ---- Render voices + FX (master gain + limiter applied inside FX chain) ----
    const bool blockHasNoteOn = [&midiMessages]
    {
        for (const auto meta : midiMessages)
            if (meta.getMessage().isNoteOn())
                return true;
        return false;
    }();
    synthEngine.renderBlockWithFx(buffer, midiMessages, 0, buffer.getNumSamples());

    if (currentPresetLoadIdForReport > 0)
    {
        ++blocksRenderedSincePresetLoad;
        if (blockHasNoteOn)
            ++notesRenderedSincePresetLoad;

        const bool currentAmpWasMetered = std::abs(synthEngine.getFx().getMeteredAmpGainDb() - getF("ampGain")) <= 0.5f;
        const bool currentNoteRendered = notesRenderedSincePresetLoad > 0;
        if (currentAmpWasMetered && currentNoteRendered)
        {
            lastRenderedPresetLoadId = currentPresetLoadIdForReport;
            lastRenderedPresetName = presetManager.getPresetName(presetManager.getCurrentPresetIndex());
            lastRenderedPresetAmpGainDb = synthEngine.getFx().getMeteredAmpGainDb();
            lastRenderTimestampMs = juce::Time::currentTimeMillis();
        }
    }

    presetManager.emitPendingUserDiapresetQualityReportIfReady();


    // ---- Output clipping meter: warn if the master bus exceeds -1 dBFS
    //      (~0.891). Throttled to once every ~1s to avoid log spam. ----
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax(peak, buffer.getMagnitude(ch, 0, buffer.getNumSamples()));
        if (peak > 0.891f)
        {
            ++clipBlocksSinceLog;
            const double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
            const int blocksPerSecond = juce::jmax(1, (int) (sr / juce::jmax(1, buffer.getNumSamples())));
            if (clipBlocksSinceLog >= blocksPerSecond)
            {
                didaAudioLog(juce::String("CLIP WARNING outputPeak=")
                    + juce::String(20.0f * std::log10(juce::jmax(1.0e-9f, peak)), 2) + " dBFS");
                clipBlocksSinceLog = 0;
            }
        }
        else
        {
            clipBlocksSinceLog = 0;
        }
    }

   #if JUCE_DEBUG
    static int s_renderTraceCounter = 0;
    if ((++s_renderTraceCounter % 512) == 0)
    {
        didaAudioLog(juce::String("render activeVoices=") + juce::String(synthEngine.getActiveVoiceCount())
            + " held=" + juce::String(synthEngine.getHeldNoteCount())
            + " instrument=" + (synthEngine.getInstrumentName().isEmpty()
                                    ? juce::String("<none/fallback>")
                                    : synthEngine.getInstrumentName()));
    }
   #endif
}

juce::AudioProcessorEditor* DiditagainProcessor::createEditor()
{
    return new DiditagainEditor(*this);
}

int DiditagainProcessor::getNumPrograms() { return presetManager.getNumPresets(); }
int DiditagainProcessor::getCurrentProgram() { return presetManager.getCurrentPresetIndex(); }
void DiditagainProcessor::setCurrentProgram(int index) { presetManager.loadPreset(index); }
const juce::String DiditagainProcessor::getProgramName(int index) { return presetManager.getPresetName(index); }
void DiditagainProcessor::changeProgramName(int, const juce::String&) {}

void DiditagainProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Wrapper tree: <DIDITAGAIN_STATE> holds the APVTS snapshot plus a
    // <PLUGIN_STATE> child carrying the selected preset / source-folder
    // identity so a DAW project recalls the exact instrument per instance.
    juce::XmlElement root("DIDITAGAIN_STATE");

    auto state = apvts.copyState();
    if (auto* apvtsXml = state.createXml().release())
        root.addChildElement(apvtsXml); // tag == apvts.state.getType()

    auto* plugin = root.createNewChildElement("PLUGIN_STATE");
    const int idx = presetManager.getCurrentPresetIndex();
    plugin->setAttribute("selectedPresetIndex",            idx);
    plugin->setAttribute("selectedPresetName",             presetManager.getPresetName(idx));
    plugin->setAttribute("selectedPresetCategory",         presetManager.getPresetCategory(idx));
    plugin->setAttribute("selectedPresetFilePath",         presetManager.getPresetFilePath(idx));
    plugin->setAttribute("selectedUserPresetFilePath",     presetManager.getPresetUserFile(idx));
    plugin->setAttribute("selectedSourceFolderPath",       presetManager.getRequestedSampleFolderPath());
    plugin->setAttribute("selectedSourceDisplayName",      presetManager.getRequestedSampleDisplayName());
    plugin->setAttribute("selectedSampleRootMidi",         presetManager.getRequestedSampleRootMidi());
    plugin->setAttribute("selectedSampleLooping",          presetManager.getRequestedSampleLooping());
    plugin->setAttribute("fallbackSynthesisEnabled",       synthEngine.isFallbackSynthesisEnabled());
    plugin->setAttribute("lastLoadedPresetWasUserDiapreset", presetManager.isCurrentPresetUserDiapreset());

    copyXmlToBinary(root, destData);
}

void DiditagainProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (! xml) return;

    if (xml->hasTagName("DIDITAGAIN_STATE"))
    {
        // 1) Restore APVTS first.
        if (auto* apvtsXml = xml->getChildByName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*apvtsXml));

        // 2) Capture the saved selection; it is replayed once the preset
        //    library has scanned (applyStartupDefaultIfNeeded below).
        if (auto* plugin = xml->getChildByName("PLUGIN_STATE"))
        {
            restoredSelection.pending                  = true;
            restoredSelection.presetIndex              = plugin->getIntAttribute("selectedPresetIndex", -1);
            restoredSelection.presetName               = plugin->getStringAttribute("selectedPresetName");
            restoredSelection.presetCategory           = plugin->getStringAttribute("selectedPresetCategory");
            restoredSelection.presetFilePath           = plugin->getStringAttribute("selectedPresetFilePath");
            restoredSelection.userPresetFilePath       = plugin->getStringAttribute("selectedUserPresetFilePath");
            restoredSelection.fallbackSynthesisEnabled = plugin->getBoolAttribute("fallbackSynthesisEnabled", false);
        }

        // Mark restored so the startup default piano never overrides project
        // state, then replay the saved selection now.
        hasRestoredState = true;
        applyStartupDefaultIfNeeded();
        return;
    }

    // Legacy projects: raw APVTS state only (no wrapper).
    if (xml->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        hasRestoredState = true;
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DiditagainProcessor();
}
