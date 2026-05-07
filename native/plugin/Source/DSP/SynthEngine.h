#pragma once
//==============================================================================
//  SynthEngine.h — Polyphonic synthesiser host.
//
//  Wraps juce::Synthesiser, owns voices, applies the global FX chain
//  after voice rendering, and exposes mono/poly + voice-stealing controls.
//==============================================================================
#include <JuceHeader.h>
#include <array>
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
    bool hasHeldNotes() const noexcept;
    bool hasActiveVoices() const noexcept;
    int getHeldNoteCount() const noexcept;
    int getActiveVoiceCount() const noexcept;
    bool canSafelyResetVoices() const noexcept;
    bool canSafelyMutateVoices(const juce::MidiBuffer& upcomingMidi) const noexcept;
    bool setMaxPolyphony(int n);
    bool setMonoMode(bool mono);

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
    struct HeldNote
    {
        bool active = false;
        float velocity = 0.0f;
    };

    static bool midiBufferHasPhraseActivity(const juce::MidiBuffer& midi) noexcept;
    void updateHeldNotes(const juce::MidiBuffer& midi);

    juce::CriticalSection voiceMutationLock;
    FxChain fx;
    std::array<std::array<HeldNote, 128>, 16> heldNotes {};
    bool    monoMode = false;
};
