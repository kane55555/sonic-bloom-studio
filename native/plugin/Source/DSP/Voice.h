#pragma once
//==============================================================================
//  Voice.h — One polyphonic voice for DIDITAGAIN STUDIO.
//
//  Each voice owns:
//    - 2 main oscillators (A, B)  — feed into either Subtractive or FM2 mode
//    - 1 sub oscillator (sine, -1 oct)
//    - 1 noise source (white / pink)
//    - 1 filter block (shared filter env + key tracking + velocity)
//    - 3 ADSR envelopes (Amp, Filter, Mod)
//    - Glide / portamento for legato playing
//
//  All audio runs sample by sample so the modulation matrix can update
//  destinations every sample without artefacts.
//==============================================================================
#include <JuceHeader.h>
#include "Oscillator.h"
#include "FilterBlock.h"
#include "Envelope.h"
#include "LFO.h"
#include "UtilityDSP.h"

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

    // ---- Per-voice configuration (called by the processor each block) ----
    void setEngineMode(EngineMode m)     noexcept { engineMode = m; }
    void setOscALevel(float v)           noexcept { oscALevel = v; }
    void setOscBLevel(float v)           noexcept { oscBLevel = v; }
    void setSubLevel(float v)            noexcept { subLevel = v; }
    void setNoiseLevel(float v)          noexcept { noiseLevel = v; }
    void setFmAmount(float v)            noexcept { fmAmount = juce::jlimit(0.0f, 12.0f, v); }
    void setFmRatio(float r)             noexcept { fmRatio  = juce::jlimit(0.25f, 16.0f, r); }
    void setGlideSeconds(float s)        noexcept { glideSeconds = juce::jmax(0.0f, s); }
    void setFilterEnvAmount(float v)     noexcept { filterEnvAmount = juce::jlimit(-1.0f, 1.0f, v); }
    void setFilterKeyTrack(float v)      noexcept { filterKeyTrack = juce::jlimit(0.0f, 1.0f, v); }
    void setBaseCutoff(float hz)         noexcept { baseCutoff = juce::jlimit(20.0f, 20000.0f, hz); }
    void setOscAPitchOffset(int semis)   noexcept { oscAPitchSemis = semis; }
    void setOscBPitchOffset(int semis)   noexcept { oscBPitchSemis = semis; }
    void setUnison(int voices, float detune, float spread) noexcept;
    void setNoiseType(int type) noexcept { noiseType = juce::jlimit(0, 1, type); }

    Oscillator&   getOscA()      noexcept { return oscA; }
    Oscillator&   getOscB()      noexcept { return oscB; }
    Oscillator&   getSubOsc()    noexcept { return subOsc; }
    FilterBlock&  getFilter()    noexcept { return filter; }
    ADSREnvelope& getAmpEnv()    noexcept { return ampEnv; }
    ADSREnvelope& getFilterEnv() noexcept { return filterEnv; }
    ADSREnvelope& getModEnv()    noexcept { return modEnv; }

private:
    // ---- Audio sources ----
    Oscillator oscA;
    Oscillator oscB;
    Oscillator subOsc;
    FilterBlock filter;
    ADSREnvelope ampEnv;
    ADSREnvelope filterEnv;
    ADSREnvelope modEnv;

    // ---- Pitch / glide ----
    float currentMidiNote   = 60.0f;
    float targetMidiNote    = 60.0f;
    float glideCoeff        = 0.0f; // computed from glideSeconds + sampleRate
    float glideSeconds      = 0.0f;
    float currentFrequency  = 440.0f;

    // ---- Note state ----
    float velocity = 0.0f;
    bool  isActive = false;

    // ---- Configuration (driven by APVTS in processor) ----
    EngineMode engineMode      = EngineMode::Subtractive;
    float oscALevel            = 0.8f;
    float oscBLevel            = 0.0f;
    float subLevel             = 0.0f;
    float noiseLevel           = 0.0f;
    float fmAmount             = 0.0f;
    float fmRatio              = 1.0f;
    float filterEnvAmount      = 0.0f;
    float filterKeyTrack       = 0.0f;
    float baseCutoff           = 8000.0f;
    int   oscAPitchSemis       = 0;
    int   oscBPitchSemis       = 0;
    int   noiseType            = 0;

    // ---- Noise generator state ----
    dida::PinkNoise noiseGen;

    double sampleRate = 44100.0;

    void recalcGlideCoeff() noexcept;
    float renderSample();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthVoice)
};
