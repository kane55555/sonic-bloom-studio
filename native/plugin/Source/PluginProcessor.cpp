#include "PluginProcessor.h"
#include "PluginEditor.h"

#define DIDA_PRESET_LOG(message) \
    juce::Logger::writeToLog(juce::String("[DIDITAGAIN preset] ") + (juce::String() << message))

DiditagainProcessor::DiditagainProcessor()
    : AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      synthEngine(),
      presetManager(*this),
      licenseClient()
{
    presetManager.onPresetLoaded = [this]()
    {
        presetLoadRequested.store(true, std::memory_order_release);
        presetLoadSerial.fetch_add(1, std::memory_order_acq_rel);
        DIDA_PRESET_LOG("load requested serial=" << presetLoadSerial.load(std::memory_order_acquire)
            << " index=" << presetManager.getCurrentPresetIndex()
            << " name=" << presetManager.getPresetName(presetManager.getCurrentPresetIndex()));
    };
}

DiditagainProcessor::~DiditagainProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout DiditagainProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Master
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"masterGain", 1}, "Master Gain",
        juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f), 0.0f));

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

    return { params.begin(), params.end() };
}

void DiditagainProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synthEngine.prepare(sampleRate, samplesPerBlock);
}

void DiditagainProcessor::releaseResources() {}

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

    const bool mono     = getF("monoMode") > 0.5f;
    const int  polyWant = juce::jlimit(1, 16, static_cast<int>(getF("polyphony")));
    const int   unisonVoices = juce::jlimit(1, 8, static_cast<int>(getF("unisonVoices")));
    const float unisonDetune = getF("unisonDetune");
    const float unisonSpread = getF("unisonSpread");

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
            observedPresetLoadSerial = latestPresetSerial;

        const auto previousAge = deferredPresetChange.queued ? deferredPresetChange.ageInBlocks : 0;
        deferredPresetChange.queued = true;
        deferredPresetChange.resetState = true;
        deferredPresetChange.monoMode = mono;
        deferredPresetChange.polyphony = polyWant;
        deferredPresetChange.presetSerial = latestPresetSerial;
        deferredPresetChange.ageInBlocks = previousAge;

        DIDA_PRESET_LOG("queued serial=" << deferredPresetChange.presetSerial
            << " mono=" << (deferredPresetChange.monoMode ? "true" : "false")
            << " poly=" << deferredPresetChange.polyphony
            << " appliedMono=" << (appliedMonoMode ? "true" : "false")
            << " appliedPoly=" << appliedPolyphony
            << " midiEvents=" << midiMessages.getNumEvents());
    }

    // Preset stepping may change mono/poly and requested polyphony every click.
    // Keep the latest request queued, but only touch JUCE's voice pool once the
    // host has stopped sending note events and all voices have fully released.
    if (deferredPresetChange.queued)
    {
        ++deferredPresetChange.ageInBlocks;
        const bool canApplyVoiceMutation = synthEngine.canSafelyMutateVoices(midiMessages);
        const bool voicePoolNeedsMutation = deferredPresetChange.monoMode != appliedMonoMode
            || (! deferredPresetChange.monoMode && deferredPresetChange.polyphony != appliedPolyphony);
        const bool sameVoicePool = ! voicePoolNeedsMutation;

        if (canApplyVoiceMutation || sameVoicePool)
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

            if (applied)
            {
                if (deferredPresetChange.resetState && canApplyVoiceMutation)
                    synthEngine.resetForPresetChange();

                DIDA_PRESET_LOG("applied serial=" << deferredPresetChange.presetSerial
                    << " mono=" << (deferredPresetChange.monoMode ? "true" : "false")
                    << " poly=" << deferredPresetChange.polyphony
                    << " mutatedVoices=" << (voicePoolNeedsMutation ? "true" : "false")
                    << " waitedBlocks=" << deferredPresetChange.ageInBlocks);

                deferredPresetChange = {};
            }
        }
        else if ((++debugBlockCounter % 128) == 0)
        {
            DIDA_PRESET_LOG("waiting serial=" << deferredPresetChange.presetSerial
                << " blocks=" << deferredPresetChange.ageInBlocks
                << " heldNotes=" << synthEngine.getHeldNoteCount()
                << " activeVoices=" << synthEngine.getActiveVoiceCount()
                << " midiEvents=" << midiMessages.getNumEvents());
        }
    }

    synthEngine.forEachSynthVoice([&](SynthVoice& v)
    {
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

    // ---- FX parameters ----
    auto& fx = synthEngine.getFx();
    fx.setSaturationDrive(getF("fxDistortionAmount"));
    fx.setSaturationMix  (getF("fxDistortionAmount") > 0.0001f ? 1.0f : 0.0f);
    fx.setChorusMix(getF("fxChorusMix"));
    fx.setDelayMix(getF("fxDelayMix"));
    fx.setDelayTime(getF("fxDelayTime"));
    fx.setDelayFeedback(getF("fxDelayFeedback"));
    fx.setReverbMix(getF("fxReverbMix"));
    fx.setReverbSize(getF("fxReverbSize"));
    fx.setEqLowDb (getF("eqLow"));
    fx.setEqMidDb (getF("eqMid"));
    fx.setEqHighDb(getF("eqHigh"));
    fx.setCompEnabled(getF("compEnabled") > 0.5f);
    fx.setCompThresholdDb(getF("compThreshold"));
    fx.setCompRatio(getF("compRatio"));
    fx.setLimiterCeilingDb(getF("limiterCeiling"));
    fx.setWetHighPassHz(getF("fxWetHighPass"));
    fx.setMasterGainDb(getF("masterGain"));

    // ---- Render voices + FX (master gain + limiter applied inside FX chain) ----
    synthEngine.renderBlockWithFx(buffer, midiMessages, 0, buffer.getNumSamples());
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
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void DiditagainProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DiditagainProcessor();
}
