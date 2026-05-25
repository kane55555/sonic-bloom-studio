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

    void prepare(double sampleRate, int samplesPerBlock);
    void resetNote() noexcept { clearCurrentNote(); reset(); }

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
    };
    static constexpr int kMaxPartials = 4;

    void clearPartials() noexcept;
    void setPartial(int idx,
                    std::unique_ptr<dida::engines::IEngineSource> engine,
                    bool enabled, float level, float pan,
                    int pitchSemis, float fineCents) noexcept;
    bool hasActivePartials() const noexcept;

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

    // ---- Per-layer "carving" filters: keep each layer in its own band
    //      so they stop fighting and start sounding like one instrument. ----
    OnePoleCarver noiseHpL, noiseHpR;   // HP ~2 kHz on noise/air
    OnePoleCarver subLp;                // LP ~250 Hz on sub
    OnePoleCarver oscBHp;               // gentle HP on Osc B to clear sample low end

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

    // Per-layer peak metering — logged ~once per second per voice.
    float peakSamp = 0.0f, peakOscB = 0.0f, peakSub = 0.0f, peakNoise = 0.0f, peakOut = 0.0f;
    int   meterFrameCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthVoice)

};
