#pragma once
//==============================================================================
//  SynthEngine.h — Polyphonic synthesiser host.
//
//  Wraps juce::Synthesiser, owns voices, applies the global FX chain
//  after voice rendering, and exposes mono/poly + voice-stealing controls.
//==============================================================================
#include <JuceHeader.h>
#include <array>
#include <memory>
#include "Voice.h"
#include "FxChain.h"
#include "SampleLibrary.h"
#include "Layers/LayerBusProcessor.h"

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
    void clearHeldNotes() noexcept;
    bool canSafelyResetVoices() const noexcept;
    bool canSafelyMutateVoices(const juce::MidiBuffer& upcomingMidi) const noexcept;
    bool setMaxPolyphony(int n);
    bool setMonoMode(bool mono);

    FxChain&  getFx()       noexcept { return fx; }
    LayerBusProcessor& getLayerBus() noexcept { return layerBus; }

    // Set the active multisample instrument by folder name (under Documents/
    // DIDITAGAIN STUDIO/Samples). Empty name = silence. Returns true on success.
    bool setInstrument(const juce::String& instrumentName);
    bool setSampleSource(const juce::String& sourcePath, int rootMidi, const juce::String& displayName = {});
    // Multi-zone (true multisample) load from an explicit file list. The roots
    // are parsed from each filename's note suffix.
    bool setMultisampleSources(const juce::Array<juce::File>& files, const juce::String& displayName);
    bool loadMultisamplePreset(const juce::String& category,
                               const juce::String& presetName,
                               const juce::String& folderPath);
    void setFallbackSynthesisEnabled(bool enabled);
    void setSampleLooping(bool shouldLoop);
    void setSampleCropLoop(float cropStart, float cropEnd,
                           float loopStart, float loopEnd,
                           float crossfadeMs, bool oneShot, bool pitchTracking);
    const juce::String& getInstrumentName() const noexcept { return currentInstrumentName; }

    // Drops the cached instrument identity so the next setInstrument/
    // setSampleSource/setMultisampleSources/loadMultisamplePreset call is
    // forced to re-read sample data from disk instead of reusing the
    // already-loaded shared_ptr. Used after the Audio Crop tab rewrites a
    // WAV so playback picks up the trimmed file immediately.
    void invalidateActiveInstrumentCache() noexcept
    {
        currentInstrumentName.clear();
        activeMultisample.reset();
    }

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

    FxChain fx;
    LayerBusProcessor layerBus;
    std::array<std::array<HeldNote, 128>, 16> heldNotes {};
    bool    monoMode = false;
    bool    fallbackSynthesisEnabled = true;
    std::shared_ptr<const dida::Multisample> activeMultisample;
    juce::String currentInstrumentName;
};
