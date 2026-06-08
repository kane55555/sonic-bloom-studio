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
    void chokeAllFxSends(float fadeMs) noexcept;
    void setFxSendReleaseMsForAll(float ms) noexcept;
    float getFxSendReleaseMs() const noexcept { return currentFxSendReleaseMs; }
    bool hasSeparatedFxSendBus() const noexcept { return true; }
    bool hasHeldNotes() const noexcept;
    bool hasActiveVoices() const noexcept;
    int getHeldNoteCount() const noexcept;
    int getActiveVoiceCount() const noexcept;
    void clearHeldNotes() noexcept;
    bool canSafelyResetVoices() const noexcept;
    bool canSafelyMutateVoices(const juce::MidiBuffer& upcomingMidi) const noexcept;
    bool setMaxPolyphony(int n);
    bool setMonoMode(bool mono);

    // Global oversampling for the synthesis engines (anti-aliasing). factorLog2:
    //   0 = off (1x, bit-identical to the legacy path),
    //   1 = 2x, 2 = 4x.
    // Changing this re-prepares the voices at the oversampled rate and rebuilds
    // the half-band IIR up/down samplers. Off by default so existing presets
    // sound identical unless the user opts in. Voices render at the oversampled
    // rate; the FX chain stays at the host rate and runs on the decimated bus.
    void setOversamplingFactor(int factorLog2);
    int  getOversamplingFactorLog2() const noexcept { return oversampleFactorLog2; }
    int  getOversamplingFactor()     const noexcept { return 1 << oversampleFactorLog2; }

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
    bool isFallbackSynthesisEnabled() const noexcept { return fallbackSynthesisEnabled; }
    void setSampleLooping(bool shouldLoop);
    void setSampleCropLoop(float cropStart, float cropEnd,
                           float loopStart, float loopEnd,
                           float crossfadeMs, bool oneShot, bool pitchTracking);
    const juce::String& getInstrumentName() const noexcept { return currentInstrumentName; }

    // ---- Zone diagnostics for the preset-quality reporter (Report 71) ----
    // Lets the reporter tell instrument silence (no zone for the played note,
    // empty sample map) apart from a gain-stage collapse, so DRY_BUS_SILENT can
    // emit an exact root cause instead of a bare warning.
    int getActiveZoneCount() const noexcept
    {
        return activeMultisample != nullptr ? (int) activeMultisample->zones.size() : 0;
    }

    // 0 = no zones loaded, 1 = an exact hard key-zone covers the note,
    // 2 = only a nearest-root fallback would play (note outside every range).
    int classifyZoneCoverageForNote(int midi, int velocity = 100) const noexcept
    {
        if (activeMultisample == nullptr || activeMultisample->zones.empty())
            return 0;
        for (const auto& z : activeMultisample->zones)
            if (velocity >= z.loVel && velocity <= z.hiVel
                && midi >= z.lowKey && midi <= z.highKey)
                return 1;
        for (const auto& z : activeMultisample->zones)
            if (midi >= z.lowKey && midi <= z.highKey)
                return 1;
        return 2;
    }

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
    juce::AudioBuffer<float> dryRenderBuffer;
    juce::AudioBuffer<float> fxSendBuffer;
    std::array<std::array<HeldNote, 128>, 16> heldNotes {};
    bool    monoMode = false;
    bool    fallbackSynthesisEnabled = true;
    float   currentFxSendReleaseMs = 80.0f;

    // ---- Oversampling state ----
    void rebuildOversampling();
    double baseSampleRate       = 44100.0;
    int    preparedBlockSize    = 512;
    int    oversampleFactorLog2 = 0;   // 0=off, 1=2x, 2=4x
    std::unique_ptr<juce::dsp::Oversampling<float>> oversamplerDry;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversamplerSend;
    juce::MidiBuffer scaledMidi;       // reused across blocks (no per-block alloc)
    std::shared_ptr<const dida::Multisample> activeMultisample;
    juce::String currentInstrumentName;
};
