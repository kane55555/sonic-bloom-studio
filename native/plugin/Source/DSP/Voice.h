#pragma once
#include <JuceHeader.h>
#include "Oscillator.h"
#include "FilterBlock.h"
#include "Envelope.h"
#include "LFO.h"

class SynthVoice : public juce::SynthesiserVoice
{
public:
    SynthVoice();

    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int newPitchWheelValue) override;
    void controllerMoved(int controllerNumber, int newControllerValue) override;
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

    void prepare(double sampleRate, int samplesPerBlock);

private:
    Oscillator oscA;
    Oscillator oscB;
    FilterBlock filter;
    ADSREnvelope ampEnv;
    ADSREnvelope filterEnv;
    ADSREnvelope modEnv;

    float currentFrequency = 440.0f;
    float velocity = 0.0f;
    double sampleRate = 44100.0;

    bool isActive = false;
};
