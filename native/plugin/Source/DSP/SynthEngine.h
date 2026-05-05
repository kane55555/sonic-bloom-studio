#pragma once
//==============================================================================
//  SynthEngine.h — Polyphonic synthesiser host.
//
//  Wraps juce::Synthesiser, owns voices, applies the global FX chain
//  after voice rendering, and exposes mono/poly + voice-stealing controls.
//==============================================================================
#include <JuceHeader.h>
#include "Voice.h"
#include "FxChain.h"

class DiditagainSynthSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote(int) override    { return true; }
    bool appliesToChannel(int) override { return true; }
};

class SynthEngine : public juce::Synthesiser
{
public:
    SynthEngine();

    void prepare(double sampleRate, int samplesPerBlock);

    // Render voices then run the global FX chain on the buffer.
    void renderBlockWithFx(juce::AudioBuffer<float>& buffer,
                           const juce::MidiBuffer& midi,
                           int startSample, int numSamples);

    void resetForPresetChange();
    void setMaxPolyphony(int n);
    void setMonoMode(bool mono);

    FxChain&  getFx()   noexcept { return fx; }

    // Apply config to every voice (called when APVTS changes).
    template <typename Fn>
    void forEachSynthVoice(Fn&& fn)
    {
        for (int i = 0; i < getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*>(getVoice(i)))
                fn(*v);
    }

    static constexpr int MAX_POLYPHONY = 16;

private:
    FxChain fx;
    bool    monoMode = false;
};
