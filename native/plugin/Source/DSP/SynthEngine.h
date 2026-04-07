#pragma once
#include <JuceHeader.h>
#include "Voice.h"

class SynthEngine : public juce::Synthesiser
{
public:
    SynthEngine();

    void prepare(double sampleRate, int samplesPerBlock);

    static constexpr int MAX_POLYPHONY = 16;

private:
    void addVoices();
};
