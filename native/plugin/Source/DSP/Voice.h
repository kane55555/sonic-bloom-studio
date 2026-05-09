#pragma once
//==============================================================================
//  Voice.h — One polyphonic voice for DIDITAGAIN STUDIO.
//
//  This is now a MULTISAMPLE player (Nexus / Kontakt style):
//    - The active "instrument" is a folder of audio one-shots, each named
//      with its root note (e.g. Brass_C3.wav, Brass_F#3_v90.wav).
//    - Notes are produced by pitch-shifting the nearest sample with linear
//      interpolation, crossfading between the two nearest root notes.
//    - The amp envelope, filter, glide, velocity response, and FX chain
//      are still applied, so existing presets keep most of their character.
//
//  The public API (set*, get*) is intentionally kept compatible with the
//  previous synthesis voice so PluginProcessor / SynthEngine don't need to
//  change. Synthesis-only setters (osc waveform, FM, unison, sub, noise,
//  pulse-width) are now no-ops.
//==============================================================================
#include <JuceHeader.h>
#include <memory>
#include "Oscillator.h"
#include "FilterBlock.h"
#include "Envelope.h"
#include "SampleLibrary.h"

// Lightweight stand-ins so legacy editor/UI code that took an Oscillator&
// reference still compiles. They accept the same calls but produce no audio
// (sample playback is now the audio source).
class LegacyOscillatorStub
{
public:
    void setWaveform(Oscillator::Waveform w) noexcept { waveform = w; }
    void setDetuneCents(float c) noexcept { detuneCents = c; }
    void setPulseWidth(float p)  noexcept { pulseWidth  = p; }
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

    // Public wrapper to expose protected SynthesiserVoice::clearCurrentNote().
    void resetNote() noexcept { clearCurrentNote(); reset(); }

    // The active multisample (set by SynthEngine). Null = silence.
    void setMultisample(std::shared_ptr<const dida::Multisample> ms) noexcept { multisample = std::move(ms); }

    // ---- Per-voice configuration (kept from synth API; most are no-ops now) ----
    void setEngineMode(EngineMode)        noexcept {}
    void setOscALevel(float)              noexcept {}
    void setOscBLevel(float)              noexcept {}
    void setSubLevel(float)               noexcept {}
    void setNoiseLevel(float)             noexcept {}
    void setFmAmount(float)               noexcept {}
    void setFmRatio(float)                noexcept {}
    void setGlideSeconds(float s)         noexcept { glideSeconds = juce::jmax(0.0f, s); recalcGlideCoeff(); }
    void setFilterEnvAmount(float v)      noexcept { filterEnvAmount = juce::jlimit(-1.0f, 1.0f, v); }
    void setFilterKeyTrack(float v)       noexcept { filterKeyTrack = juce::jlimit(0.0f, 1.0f, v); }
    void setBaseCutoff(float hz)          noexcept { baseCutoff = juce::jlimit(20.0f, 20000.0f, hz); }
    void setOscAPitchOffset(int semis)    noexcept { pitchOffsetSemis = semis; }
    void setOscBPitchOffset(int)          noexcept {}
    void setUnison(int, float, float)     noexcept {}
    void setNoiseType(int)                noexcept {}

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

    // Render a stereo sample pair from a single zone at a given playback rate.
    void readZone(const dida::SampleZone& z, double readPos, float& outL, float& outR) const noexcept;

    // ---- Sample source ----
    std::shared_ptr<const dida::Multisample> multisample;
    const dida::SampleZone* loZone = nullptr;
    const dida::SampleZone* hiZone = nullptr;
    float zoneXfade = 0.0f;
    double loReadPos = 0.0;
    double hiReadPos = 0.0;
    double loStep = 1.0;   // playback rate for low zone
    double hiStep = 1.0;
    bool   loFinished = true;
    bool   hiFinished = true;

    // ---- DSP blocks ----
    FilterBlock filter;
    ADSREnvelope ampEnv;
    ADSREnvelope filterEnv;
    ADSREnvelope modEnv;

    LegacyOscillatorStub oscAStub, oscBStub, subStub;

    // ---- Pitch / glide ----
    float currentMidiNote   = 60.0f;
    float targetMidiNote    = 60.0f;
    float glideCoeff        = 0.0f;
    float glideSeconds      = 0.0f;

    // ---- Note state ----
    float velocity = 0.0f;
    bool  isActive = false;
    int   pitchOffsetSemis = 0;

    // ---- Filter modulation ----
    float filterEnvAmount = 0.0f;
    float filterKeyTrack  = 0.0f;
    float baseCutoff      = 8000.0f;

    // Phase for the simple sine fallback used when no sample is loaded
    // (keeps non-sample factory presets audible until the layered engine
    // is fully wired through Voice).
    double sineFallbackPhase = 0.0;

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthVoice)
};
