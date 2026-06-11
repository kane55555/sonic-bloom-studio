#pragma once
//==============================================================================
//  Voice.h — One polyphonic voice for DIDITAGAIN STUDIO.
//
//  Hybrid voice: plays a multisample (Layer 1) and mixes in additional
//  synthesis layers — Osc B, Sub, Noise — plus optional FM modulation of
//  Osc B by an internal modulator. All synthesis setters now have real
//  effects on the rendered sound.
//==============================================================================
#include <JuceHeader.h>
#include <memory>
#include <array>
#include <random>
#include "Oscillator.h"
#include "FilterBlock.h"
#include "Envelope.h"
#include "SampleLibrary.h"
#include "Synthesis/UnisonEngine.h"
#include "Synthesis/HarmonicExciter.h"
#include "Synthesis/StereoSpread.h"
#include "Layers/LayerEQCarver.h"
#include "Engines/IEngineSource.h"


// Lightweight stand-ins so legacy editor/UI code that took an Oscillator&
// reference still compiles. They route waveform/detune/pulse-width into the
// owning SynthVoice via callbacks installed by the voice constructor.
class LegacyOscillatorStub
{
public:
    std::function<void(Oscillator::Waveform)> onWaveform;
    std::function<void(float)>                onDetune;
    std::function<void(float)>                onPulseWidth;

    void setWaveform(Oscillator::Waveform w) noexcept { if (onWaveform)    onWaveform(w);    waveform = w; }
    void setDetuneCents(float c)             noexcept { if (onDetune)      onDetune(c);      detuneCents = c; }
    void setPulseWidth(float p)              noexcept { if (onPulseWidth)  onPulseWidth(p);  pulseWidth  = p; }
private:
    Oscillator::Waveform waveform = Oscillator::Waveform::Saw;
    float detuneCents = 0.0f;
    float pulseWidth  = 0.5f;
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    enum class EngineMode { Subtractive, FM2, FM4, Wavetable, Layered };

    SynthVoice();

    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int newPitchWheelValue) override;
    void controllerMoved(int controllerNumber, int newControllerValue) override;
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

    // During SynthEngine::renderBlockWithFx the normal JUCE dry render still
    // writes to outputBuffer, while this context collects a separate post-amp
    // FX-send bus gated by the voice's own short send envelope.
    static void beginFxSendRender(juce::AudioBuffer<float>* fxSendBuffer) noexcept;
    static void endFxSendRender() noexcept;

    void prepare(double sampleRate, int samplesPerBlock);
    void resetNote() noexcept { clearCurrentNote(); reset(); }
    void setFxSendReleaseMs(float ms) noexcept { fxSendReleaseMs = juce::jlimit(1.0f, 500.0f, ms); }
    float getFxSendReleaseMs() const noexcept { return fxSendReleaseMs; }
    void chokeFxSend(float fadeMs) noexcept;

    void setMultisample(std::shared_ptr<const dida::Multisample> ms) noexcept { multisample = std::move(ms); }
    void setFallbackSynthesisEnabled(bool enabled) noexcept { fallbackSynthesisEnabled = enabled; }
    void setSampleLooping(bool shouldLoop) noexcept { sampleLooping = shouldLoop; }

    // ---- Vintage / voice-card calibration ----
    void setVoiceCardIndex(int idx) noexcept { voiceCardIndex = idx; }
    void setVintageAmount(float a) noexcept  { vintageAmount = juce::jlimit(0.0f, 1.0f, a); }
    int  getVoiceCardIndex() const noexcept  { return voiceCardIndex; }

    // ---- New hybrid-synth controls (unison/exciter/spread) ----
    void setUnisonRender(int voices, float detune, float spread, float drift) noexcept
    {
        unisonRenderVoices = voices;
        unisonRenderDetune = detune;
        unisonRenderSpread = spread;
        unisonRenderDrift  = drift;
        unison.setConfig(voices, detune, spread, drift);
    }
    void setExciterAmount(float a) noexcept       { exciter.setAmount(a); }
    void setStereoSpreadAmount(float a) noexcept  { spreader.setAmount(a); }

    // ---- Multi-engine partials (additive, mixed before the filter) ----
    struct PartialSlot
    {
        std::unique_ptr<dida::engines::IEngineSource> engine;
        bool  enabled    = false;
        float level      = 1.0f;
        float pan        = 0.0f;
        int   pitchSemis = 0;
        float fineCents  = 0.0f;
        // AI Texture v0.1: neural texture partials are scaled by a live UI
        // "Texture Amount" so amount=0 fully mutes the cached texture.
        bool  isNeuralTexture = false;
    };
    static constexpr int kMaxPartials = 4;

    void clearPartials() noexcept;
    void setPartial(int idx,
                    std::unique_ptr<dida::engines::IEngineSource> engine,
                    bool enabled, float level, float pan,
                    int pitchSemis, float fineCents,
                    bool isNeuralTexture = false) noexcept;
    bool hasActivePartials() const noexcept;

    // AI Texture v0.1 — live "Texture Amount" applied to neural texture partials
    // only. This setter is real-time-safe: it only writes a single float. It
    // NEVER allocates, locks, parses JSON, loads files, or touches the UI.
    //
    // Gain mapping (gain hardening):
    //   * enabled == false   -> 0.0  (fully muted, never feeds FX sends)
    //   * amount01 <= 0       -> 0.0  (fully muted, never feeds FX sends)
    //   * amount01 in (0, 1]  -> shaped, dimensionless 0..1 reducer
    //
    // The amount uses a quadratic taper for finer low-level control. It is a
    // PURE REDUCER (never above 1.0), so it can only attenuate. amount=1 is the
    // preset's intended texture level, NOT unity output: the per-preset texture
    // trim (default -18 dB, hard-capped at the safe max -9 dB) is owned by
    // NeuralTextureEngine, so the audible texture can never exceed -9 dB and
    // amount=1 still sits comfortably under the main sample.
    static constexpr float kNeuralSafeMaxLin = 0.354813389f; // -9 dBFS (engine cap)
    void setNeuralTextureLiveGain(bool enabled, float amount01) noexcept
    {
        if (! enabled || amount01 <= 0.0f) { neuralTextureLiveGain = 0.0f; return; }
        const float a = juce::jlimit(0.0f, 1.0f, amount01);
        neuralTextureLiveGain = a * a; // shaped curve: finer control at low levels
    }

    // AI Texture v0.2 — DEBUG audition solo. When true, only neuralTextureCached
    // partials are audible: the main multisample, layer-2, fallback synth and all
    // non-neural partials are muted for this voice. This is a transient debug
    // override (never written to a preset) so it cannot permanently alter sound.
    void setNeuralTextureSolo(bool s) noexcept { soloNeuralTexture = s; }

    // Diagnostic-only (off the audio thread): the most recent block peak (linear)
    // produced by any active neural texture partial in this voice. Used by the
    // preset-quality reporter for the AI Texture diagnostic section. Reads an
    // atomic from the engine; never alters DSP state.
    float getNeuralTexturePeakLinear() const noexcept
    {
        float pk = 0.0f;
        for (const auto& slot : partials_)
            if (slot.enabled && slot.isNeuralTexture && slot.engine != nullptr)
                pk = juce::jmax(pk, slot.engine->getLastPeakLinear());
        return pk;
    }

    // Diagnostic-only: peak (linear) of the main multisample / PCM body of this
    // voice since the last noteOn. Lets the reporter prove the multisample is the
    // primary sound and measure the neural texture's relative contribution.
    float getMainSamplePeakLinear() const noexcept { return mainSamplePeakLin_.load(); }

    // ===== Live-render diagnostics (Choir zero-buffer investigation) =========
    // Captured once per render block on the audio thread and read off-thread by
    // the preset-quality reporter. PURELY DIAGNOSTIC: never alters DSP. Proves
    // exactly where a decoded-but-healthy sample disappears during live
    // playback (reader/crop/playhead vs envelope vs gain/layer routing).
    enum class CalibrationCandidateSource { activeVoiceRender = 0, reportProbe = 1, loadProbe = 2, oneShotDiagnostic = 3 };
    struct LiveRenderSnapshot
    {
        bool   valid = false;
        unsigned long long seq = 0;     // monotonic block sequence (recency)
        int    presetLoadId = 0;        // preset/load generation that produced this block
        int    blocksSincePresetLoad = 0;
        int    calibrationCandidateVoiceId = -1;
        int    calibrationCandidateNoteLifetimeId = -1;
        int    calibrationCandidateNoteAgeBlocks = -1;
        juce::String calibrationCandidateSource { "reportProbe" };
        bool   calibrationCandidateWasProbe = true;
        bool   calibrationCandidateWasReaderReset = true;

        // Played note / zone selection.
        int    playedMidiNote = -1;
        float  playedVelocity = 0.0f;
        int    selectedZoneRoot = -1;
        int    selectedZoneDistanceSemitones = -1;

        // Live sample-reader state for the multisample body (lo zone).
        double sampleReaderSourceStartSample = 0.0;
        int    sampleReaderSourceLengthSamples = 0;
        double sampleReaderPlayheadBeforeRender = 0.0;
        double sampleReaderPlayheadAfterRender = 0.0;
        int    sampleReaderRequestedNumSamples = 0;
        int    sampleReaderActualSamplesRead = 0;
        bool   sampleReaderLoopEnabled = false;
        bool   sampleReaderAtEndBeforeRender = false;
        bool   sampleReaderAtEndAfterRender = false;

        // Crop / start / end region (samples).
        int    zoneStartSample = 0;
        int    zoneEndSample = 0;
        int    zoneCropStartSample = 0;
        int    zoneCropEndSample = 0;
        double effectivePlaybackStartSample = 0.0;
        double effectivePlaybackEndSample = 0.0;

        // Live buffer probes (dB / counts).
        float  liveReaderBufferPeakDbBeforeEnvelope = -120.0f;
        float  liveReaderBufferPeakDbAfterEnvelope = -120.0f;
        float  liveReaderBufferPeakDbAfterGain = -120.0f;
        int    liveReaderBufferNonZeroSampleCount = 0;

        // Envelope / gain state.
        int    ampEnvelopeStage = 0;            // 0=Idle..4=Release
        juce::String ampEnvelopeStateName { "Idle" };
        float  ampEnvelopeCurrentGain = 0.0f;
        float  ampEnvelopeAttackMs = 0.0f;
        float  ampEnvelopeDecayMs = 0.0f;
        float  ampEnvelopeSustain = 0.0f;
        float  ampEnvelopeReleaseMs = 0.0f;
        float  voiceGainDb = -120.0f;           // per-voice VCA/card trim
        float  layerGainDb = -120.0f;           // sample (oscA) layer level
        float  finalVoiceGainDb = -120.0f;      // combined static voice path gain
    };

    LiveRenderSnapshot getLiveRenderSnapshot() const noexcept;
    LiveRenderSnapshot getCalibrationCandidateSnapshot() const noexcept;
    void setLiveRenderPresetContext(int loadId, int blocksSinceLoad,
                                    CalibrationCandidateSource source = CalibrationCandidateSource::reportProbe,
                                    bool wasProbe = true,
                                    bool wasReaderReset = true) noexcept
    {
        liveRenderPresetLoadId_.store(loadId);
        liveRenderBlocksSinceLoad_.store(blocksSinceLoad);
        liveRenderCandidateSource_.store((int) source);
        liveRenderCandidateWasProbe_.store(wasProbe);
        liveRenderCandidateWasReaderReset_.store(wasReaderReset);
    }



    // Diagnostic-only: static intrinsic peak (linear) of the loaded neural
    // texture buffer in this voice, available even when no note is sounding.
    float getNeuralTextureIntrinsicPeakLinear() const noexcept
    {
        float pk = 0.0f;
        for (const auto& slot : partials_)
            if (slot.enabled && slot.isNeuralTexture && slot.engine != nullptr)
                pk = juce::jmax(pk, slot.engine->getStaticPeakLinear());
        return pk;
    }

    float getSupportBodyPeakLinear() const noexcept
    {
        float pk = 0.0f;
        for (const auto& slot : partials_)
            if (slot.enabled && ! slot.isNeuralTexture && slot.engine != nullptr
                && slot.engine->type() != dida::engines::EngineType::Pcm)
                pk = juce::jmax(pk, slot.engine->getLastPeakLinear() * slot.level);
        return pk;
    }

    float getSupportBodyGainDb() const noexcept
    {
        float gain = 0.0f;
        for (const auto& slot : partials_)
            if (slot.enabled && ! slot.isNeuralTexture && slot.engine != nullptr
                && slot.engine->type() != dida::engines::EngineType::Pcm)
                gain = juce::jmax(gain, slot.level);
        return gain > 1.0e-6f ? juce::Decibels::gainToDecibels(gain) : -120.0f;
    }

    bool isSupportBodyActive() const noexcept
    {
        for (const auto& slot : partials_)
            if (slot.enabled && ! slot.isNeuralTexture && slot.engine != nullptr
                && slot.engine->type() != dida::engines::EngineType::Pcm)
                return true;
        return false;
    }

    bool hasSupportBodyVoiceStarted() const noexcept
    {
        for (const auto& slot : partials_)
            if (slot.enabled && ! slot.isNeuralTexture && slot.engine != nullptr
                && slot.engine->type() != dida::engines::EngineType::Pcm
                && slot.engine->getStaticPeakLinear() > 0.0f)
                return true;
        return false;
    }

    // Diagnostic-only: the support/body partial's amp-envelope stage. Returns
    // the first active support partial's stage, or "n/a" if none exist.
    juce::String getSupportBodyEnvelopeState() const noexcept
    {
        for (const auto& slot : partials_)
            if (slot.enabled && ! slot.isNeuralTexture && slot.engine != nullptr
                && slot.engine->type() != dida::engines::EngineType::Pcm)
                return slot.engine->getEnvelopeStateName();
        return "n/a";
    }

    // Diagnostic-only: true once a support/body partial has rendered at least one
    // block since its last noteOn. The reporter uses this to gate reportEligible.
    bool hasSupportBodyRenderedBlock() const noexcept
    {
        for (const auto& slot : partials_)
            if (slot.enabled && ! slot.isNeuralTexture && slot.engine != nullptr
                && slot.engine->type() != dida::engines::EngineType::Pcm
                && slot.engine->hasRenderedBlockSinceNoteOn())
                return true;
        return false;
    }

    // --- Crop / loop metadata (fractions of the buffer length, 0..1) ---
    void setCropRange(float start01, float end01) noexcept {
        cropStartFrac = juce::jlimit(0.0f, 1.0f, start01);
        cropEndFrac   = juce::jlimit(cropStartFrac, 1.0f, end01);
    }
    void setLoopRange(float lstart01, float lend01) noexcept {
        loopStartFrac = juce::jlimit(0.0f, 1.0f, lstart01);
        loopEndFrac   = juce::jlimit(loopStartFrac, 1.0f, lend01);
    }
    void setLoopCrossfadeMs(float ms)    noexcept { loopCrossfadeMs = juce::jlimit(0.0f, 200.0f, ms); }
    void setOneShotMode(bool b)          noexcept { oneShotMode = b; }
    void setPitchTracking(bool b)        noexcept { pitchTracking = b; }


    // ---- Per-voice configuration ----
    void setEngineMode(EngineMode m)      noexcept { engineMode = m; }
    void setOscALevel(float v)            noexcept { oscALevel = juce::jlimit(0.0f, 1.0f, v); }
    void setOscBLevel(float v)            noexcept { oscBLevel = juce::jlimit(0.0f, 1.0f, v); }
    void setSubLevel(float v)             noexcept { subLevel  = juce::jlimit(0.0f, 1.0f, v); }
    void setNoiseLevel(float v)           noexcept { noiseLevel = juce::jlimit(0.0f, 1.0f, v); }
    void setFmAmount(float v)             noexcept { fmAmount = juce::jlimit(0.0f, 12.0f, v); }
    void setFmRatio(float v)              noexcept { fmRatio  = juce::jlimit(0.25f, 16.0f, v); }
    void setGlideSeconds(float s)         noexcept { glideSeconds = juce::jmax(0.0f, s); recalcGlideCoeff(); }
    void setFilterEnvAmount(float v)      noexcept { filterEnvAmount = juce::jlimit(-1.0f, 1.0f, v); }
    void setFilterKeyTrack(float v)       noexcept { filterKeyTrack = juce::jlimit(0.0f, 1.0f, v); }
    void setBaseCutoff(float hz)          noexcept { baseCutoff = juce::jlimit(20.0f, 20000.0f, hz); }
    void setOscAPitchOffset(int semis)    noexcept { pitchOffsetSemis = semis; }
    void setOscBPitchOffset(int semis)    noexcept { oscBPitchOffsetSemis = semis; }
    void setUnison(int v, float d, float s) noexcept
    {
        unisonVoices = juce::jlimit(1, 8, v);
        unisonDetune = juce::jlimit(0.0f, 1.0f, d);
        unisonSpread = juce::jlimit(0.0f, 1.0f, s);
    }
    void setNoiseType(int t)              noexcept { noiseType = (t == 1 ? 1 : 0); }

    // ---- Per-layer EQ role wiring (v2.1 blendMode contract) ----
    //
    // Full role table is encoded inside LayerRoleCarver — see that header.
    // body / warmth / air / texture / sub / lead / full are all supported
    // and apply HP+LP+trim as one operation per layer.
    void setLayer2EqRole(const juce::String& role) noexcept { oscBCarver.setRole(role); }
    void setLayer3EqRole(const juce::String& role) noexcept
    {
        // Air/texture/warmth roles are meaningful for noise; everything else
        // falls back to "air" so the noise layer always lives above the body.
        const auto r = role.trim().toLowerCase();
        const juce::String use = (r == "air" || r == "texture" || r == "warmth") ? r : juce::String("air");
        noiseCarverL.setRole(use);
        noiseCarverR.setRole(use);
    }
    void setLayer4EqRole(const juce::String& role) noexcept
    {
        // Sub layer: prefer sub/warmth/body — anything else collapses to sub.
        const auto r = role.trim().toLowerCase();
        const juce::String use = (r == "sub" || r == "warmth" || r == "body") ? r : juce::String("sub");
        subCarver.setRole(use);
    }

    // ---- followMainEnvelope wiring ----
    //
    // When enabled, a soft fade-in is applied to the layer so reinforcement
    // sine/triangle layers can't enter as a click ahead of the main sample,
    // and the layer release will not finish before the main amp env. The
    // fade duration is max(minFadeMs, mainAttackMs * 0.5).
    void setLayer2FollowMain(bool follow, float mainAttackMs) noexcept
    { oscBFollowMain = follow; oscBFollowFadeMs = juce::jmax(8.0f, 0.5f * mainAttackMs); }
    void setLayer3FollowMain(bool follow, float mainAttackMs) noexcept
    { noiseFollowMain = follow; noiseFollowFadeMs = juce::jmax(12.0f, 0.5f * mainAttackMs); }
    void setLayer4FollowMain(bool follow, float mainAttackMs) noexcept
    { subFollowMain = follow; subFollowFadeMs = juce::jmax(10.0f, 0.5f * mainAttackMs); }


    LegacyOscillatorStub& getOscA()      noexcept { return oscAStub; }
    LegacyOscillatorStub& getOscB()      noexcept { return oscBStub; }
    LegacyOscillatorStub& getSubOsc()    noexcept { return subStub;  }
    FilterBlock&  getFilter()    noexcept { return filter; }
    ADSREnvelope& getAmpEnv()    noexcept { return ampEnv; }
    ADSREnvelope& getFilterEnv() noexcept { return filterEnv; }
    ADSREnvelope& getModEnv()    noexcept { return modEnv; }

private:
    void recalcGlideCoeff() noexcept;
    void reset() noexcept;
    void beginFxSendRelease(float releaseMs) noexcept;
    float nextFxSendGain() noexcept;
    void readZone(const dida::SampleZone& z, double readPos, float& outL, float& outR) const noexcept;

    float renderOscShape(Oscillator::Waveform w, float phase01, float pw) const noexcept;
    float nextNoiseSample() noexcept;

    // ---- Sample source ----
    std::shared_ptr<const dida::Multisample> multisample;
    const dida::SampleZone* loZone = nullptr;
    const dida::SampleZone* hiZone = nullptr;
    float zoneXfade = 0.0f;
    double loReadPos = 0.0, hiReadPos = 0.0;
    double loStep = 1.0, hiStep = 1.0;
    bool   loFinished = true, hiFinished = true;

    // ---- DSP blocks ----
    FilterBlock filter;
    ADSREnvelope ampEnv, filterEnv, modEnv;

    LegacyOscillatorStub oscAStub, oscBStub, subStub;

    // ---- Pitch / glide ----
    float currentMidiNote   = 60.0f;
    float targetMidiNote    = 60.0f;
    float glideCoeff        = 0.0f;
    float glideSeconds      = 0.0f;

    // ---- Note state ----
    float velocity = 0.0f;
    bool  isActive = false;
    int   pitchOffsetSemis     = 0;
    int   oscBPitchOffsetSemis = 0;

    // Independent post-amp FX-send gate. The dry voice can keep following the
    // musical amp release, but delay/reverb input is choked quickly after note-off.
    float fxSendLevel = 0.0f;
    float fxSendTarget = 0.0f;
    float fxSendReleaseMs = 80.0f;
    float fxSendReleaseStep = 0.0f;
    int   fxSendReleaseSamples = 0;
    int   fxSendReleaseCounter = 0;
    bool  fxSendActive = false;
    bool  noteReleasedForFxSend = false;

    // ---- Layer levels ----
    float oscALevel  = 1.0f;
    float oscBLevel  = 0.0f;
    float subLevel   = 0.0f;
    float noiseLevel = 0.0f;

    // ---- Osc B / FM ----
    EngineMode engineMode = EngineMode::Subtractive;
    Oscillator::Waveform oscAWave = Oscillator::Waveform::Saw;
    Oscillator::Waveform oscBWave = Oscillator::Waveform::Sine;
    float oscADetuneCents = 0.0f;
    float oscBDetuneCents = 0.0f;
    float oscAPulseWidth  = 0.5f;
    float oscBPulseWidth  = 0.5f;
    double oscBPhase = 0.0;
    double subPhase  = 0.0;
    double fmModPhase = 0.0;
    // Extra per-operator phases for FM4 serial algorithm (op4 -> op3 -> op2 -> carrier).
    double fmOp3Phase = 0.0;
    double fmOp4Phase = 0.0;
    // Last-sample feedback memory for op1 (gives FM2/FM4 the metallic edge
    // and prevents the carrier from collapsing back to a pure sine).
    float  fmFeedbackZ = 0.0f;
    float fmAmount = 0.0f;
    float fmRatio  = 1.0f;

    // Unison (informational; not yet rendered as multi-voice within one Voice)
    int   unisonVoices = 1;
    float unisonDetune = 0.0f;
    float unisonSpread = 0.0f;

    // ---- Noise ----
    int noiseType = 0; // 0=white, 1=pink
    std::mt19937 noiseRng { 0x1234abcd };
    float pinkB0 = 0.0f, pinkB1 = 0.0f, pinkB2 = 0.0f;

    // ---- Per-layer "carving" filters (role-aware HP+LP+trim) ----
    LayerRoleCarver noiseCarverL, noiseCarverR;
    LayerRoleCarver subCarver;
    LayerRoleCarver oscBCarver;

    // followMainEnvelope: per-layer soft fade-in ramps so reinforcement
    // sine/triangle layers ease in instead of beeping ahead of the main
    // sample attack. fadeMs is set per-preset; samplesRemaining decrements
    // each tick and the resulting (1.0 - remain/total) gain is applied.
    bool  oscBFollowMain   = true,  noiseFollowMain   = true,  subFollowMain   = true;
    float oscBFollowFadeMs = 12.0f, noiseFollowFadeMs = 18.0f, subFollowFadeMs = 12.0f;
    int   oscBFadeSamplesRemaining = 0, noiseFadeSamplesRemaining = 0, subFadeSamplesRemaining = 0;
    int   oscBFadeSamplesTotal     = 0, noiseFadeSamplesTotal     = 0, subFadeSamplesTotal     = 0;

    // Micro-timing offsets (samples) per layer — tiny random delays
    // (0.5-8 ms) reduce the "stacked WAV" feeling and add ensemble realism.
    int   oscBStartOffsetSamples = 0;
    int   subStartOffsetSamples  = 0;
    int   noiseStartOffsetSamples = 0;
    int   sampleTickCounter = 0;




    // ---- Filter modulation ----
    float filterEnvAmount = 0.0f;
    float filterKeyTrack  = 0.0f;
    float baseCutoff      = 8000.0f;

    double sineFallbackPhase = 0.0;
    bool fallbackSynthesisEnabled = true;
    bool sampleLooping = false;

    // Crop/loop metadata (fractions of buffer length 0..1)
    float cropStartFrac    = 0.0f;
    float cropEndFrac      = 1.0f;
    float loopStartFrac    = 0.2f;
    float loopEndFrac      = 0.95f;
    float loopCrossfadeMs  = 20.0f;
    bool  oneShotMode      = false;
    bool  pitchTracking    = true;

    double sampleRate = 44100.0;

    // Vintage / analog voice-card state
    int   voiceCardIndex = 0;
    float vintageAmount  = 0.25f;   // mild vintage by default
    double driftPhase    = 0.0;     // slow analog pitch drift (0..1)

    // Hybrid synth helpers
    dida::UnisonEngine    unison;
    dida::HarmonicExciter exciter;
    dida::StereoSpread    spreader;
    int   unisonRenderVoices = 1;
    float unisonRenderDetune = 0.0f, unisonRenderSpread = 0.0f, unisonRenderDrift = 0.0f;

    // Multi-engine partials and their scratch buffer (sized in prepare()).
    std::array<PartialSlot, kMaxPartials> partials_;
    juce::AudioBuffer<float> partialScratch;
    int preparedBlockSize = 512;
    // AI Texture v0.1 live amount (0..1) — scales neural texture partials only.
    float neuralTextureLiveGain = 1.0f;
    // AI Texture v0.2 debug solo — mutes everything except neural texture partials.
    bool  soloNeuralTexture = false;

    // Per-layer peak metering — logged ~once per second per voice.
    float peakSamp = 0.0f, peakOscB = 0.0f, peakSub = 0.0f, peakNoise = 0.0f, peakOut = 0.0f;
    int   meterFrameCounter = 0;

    // Diagnostic-only: main multisample/PCM body peak (linear), held since the
    // last noteOn (NOT reset on the per-second meter cadence). Read off-thread
    // by the preset-quality reporter for mainSamplePeakDb.
    std::atomic<float> mainSamplePeakLin_ { 0.0f };

    // Live-render telemetry storage (written on the audio thread, read off-thread
    // by the preset-quality reporter). Each field is atomic so the diagnostic
    // read is race-free. Diagnostic only: never feeds DSP.
    struct LiveRenderAtomics
    {
        std::atomic<bool>  valid { false };
        std::atomic<unsigned long long> seq { 0 };
        std::atomic<int>   presetLoadId { 0 };
        std::atomic<int>   blocksSincePresetLoad { 0 };
        std::atomic<int>   calibrationCandidateVoiceId { -1 };
        std::atomic<int>   calibrationCandidateNoteLifetimeId { -1 };
        std::atomic<int>   calibrationCandidateNoteAgeBlocks { -1 };
        std::atomic<int>   calibrationCandidateSource { (int) CalibrationCandidateSource::reportProbe };
        std::atomic<bool>  calibrationCandidateWasProbe { true };
        std::atomic<bool>  calibrationCandidateWasReaderReset { true };
        std::atomic<int>   playedMidiNote { -1 };
        std::atomic<float> playedVelocity { 0.0f };
        std::atomic<int>   selectedZoneRoot { -1 };
        std::atomic<int>   selectedZoneDistanceSemitones { -1 };
        std::atomic<double> sampleReaderSourceStartSample { 0.0 };
        std::atomic<int>    sampleReaderSourceLengthSamples { 0 };
        std::atomic<double> sampleReaderPlayheadBeforeRender { 0.0 };
        std::atomic<double> sampleReaderPlayheadAfterRender { 0.0 };
        std::atomic<int>    sampleReaderRequestedNumSamples { 0 };
        std::atomic<int>    sampleReaderActualSamplesRead { 0 };
        std::atomic<bool>   sampleReaderLoopEnabled { false };
        std::atomic<bool>   sampleReaderAtEndBeforeRender { false };
        std::atomic<bool>   sampleReaderAtEndAfterRender { false };
        std::atomic<int>    zoneStartSample { 0 };
        std::atomic<int>    zoneEndSample { 0 };
        std::atomic<int>    zoneCropStartSample { 0 };
        std::atomic<int>    zoneCropEndSample { 0 };
        std::atomic<double> effectivePlaybackStartSample { 0.0 };
        std::atomic<double> effectivePlaybackEndSample { 0.0 };
        std::atomic<float>  liveReaderBufferPeakDbBeforeEnvelope { -120.0f };
        std::atomic<float>  liveReaderBufferPeakDbAfterEnvelope { -120.0f };
        std::atomic<float>  liveReaderBufferPeakDbAfterGain { -120.0f };
        std::atomic<int>    liveReaderBufferNonZeroSampleCount { 0 };
        std::atomic<int>    ampEnvelopeStage { 0 };
        std::atomic<float>  ampEnvelopeCurrentGain { 0.0f };
        std::atomic<float>  ampEnvelopeAttackMs { 0.0f };
        std::atomic<float>  ampEnvelopeDecayMs { 0.0f };
        std::atomic<float>  ampEnvelopeSustain { 0.0f };
        std::atomic<float>  ampEnvelopeReleaseMs { 0.0f };
        std::atomic<float>  voiceGainDb { -120.0f };
        std::atomic<float>  layerGainDb { -120.0f };
        std::atomic<float>  finalVoiceGainDb { -120.0f };
    };
    LiveRenderAtomics liveTel_;
    LiveRenderAtomics calibrationTel_;
    std::atomic<int> liveRenderPresetLoadId_ { 0 };
    std::atomic<int> liveRenderBlocksSinceLoad_ { 0 };
    std::atomic<int> liveRenderCandidateSource_ { (int) CalibrationCandidateSource::reportProbe };
    std::atomic<bool> liveRenderCandidateWasProbe_ { true };
    std::atomic<bool> liveRenderCandidateWasReaderReset_ { true };
    int voiceId_ = -1;
    int noteLifetimeId_ = 0;
    int noteAgeBlocks_ = 0;

    static int allocateVoiceId() noexcept
    {
        static std::atomic<int> nextVoiceId { 1 };
        return nextVoiceId.fetch_add(1);
    }

    static int allocateNoteLifetimeId() noexcept
    {
        static std::atomic<int> nextNoteLifetimeId { 1 };
        return nextNoteLifetimeId.fetch_add(1);
    }




    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthVoice)

};
