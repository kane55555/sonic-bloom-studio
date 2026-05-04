// SynthSmokeTests.cpp — instantiate the processor and render a few blocks
// of audio to ensure no crashes / NaNs leak through. Compile by linking
// against the plugin sources as a static library plus juce_audio_basics.
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"

int main()
{
    DiditagainProcessor proc;
    proc.prepareToPlay(48000.0, 256);

    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);

    int failures = 0;
    for (int block = 0; block < 50; ++block)
    {
        buffer.clear();
        proc.processBlock(buffer, midi);
        midi.clear();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* d = buffer.getReadPointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (! std::isfinite(d[i])) { ++failures; break; }
        }
    }

    proc.releaseResources();
    std::printf("SynthSmokeTests: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
