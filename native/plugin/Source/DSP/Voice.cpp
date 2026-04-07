#include "Voice.h"

SynthVoice::SynthVoice() {}

bool SynthVoice::canPlaySound(juce::SynthesiserSound*)
{
    return true; // Accept all sounds for now
}

void SynthVoice::startNote(int midiNoteNumber, float vel, juce::SynthesiserSound*, int)
{
    currentFrequency = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    velocity = vel;
    isActive = true;

    oscA.setFrequency(currentFrequency);
    oscB.setFrequency(currentFrequency);
    ampEnv.noteOn();
    filterEnv.noteOn();
    modEnv.noteOn();
}

void SynthVoice::stopNote(float, bool allowTailOff)
{
    ampEnv.noteOff();
    filterEnv.noteOff();
    modEnv.noteOff();

    if (!allowTailOff)
    {
        clearCurrentNote();
        isActive = false;
    }
}

void SynthVoice::pitchWheelMoved(int) {}
void SynthVoice::controllerMoved(int, int) {}

void SynthVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!isActive) return;

    for (int sample = startSample; sample < startSample + numSamples; ++sample)
    {
        float ampEnvValue = ampEnv.getNextSample();

        if (!ampEnv.isActive())
        {
            clearCurrentNote();
            isActive = false;
            break;
        }

        float oscASample = oscA.getNextSample();
        float oscBSample = oscB.getNextSample();
        float mixedSample = (oscASample * 0.7f + oscBSample * 0.3f);

        // Apply filter
        float filteredSample = filter.processSample(mixedSample);

        float finalSample = filteredSample * ampEnvValue * velocity;

        // Stereo output
        for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
            outputBuffer.addSample(channel, sample, finalSample);
    }
}

void SynthVoice::prepare(double sr, int)
{
    sampleRate = sr;
    oscA.prepare(sr);
    oscB.prepare(sr);
    filter.prepare(sr);
    ampEnv.prepare(sr);
    filterEnv.prepare(sr);
    modEnv.prepare(sr);
}
