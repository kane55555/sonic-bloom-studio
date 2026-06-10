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

    // Diagnostic-only: max recent neural-texture block peak (linear) across all
    // voices. Used by the preset-quality reporter; reads atomics, no DSP change.
    float getNeuralTexturePeakLinear() noexcept
    {
        float pk = 0.0f;
        for (int i = 0; i < getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*>(getVoice(i)))
                pk = juce::jmax(pk, v->getNeuralTexturePeakLinear());
        return pk;
    }

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

    // ---- Report test-note probe (Report 72 / BUG 4) ----------------------
    // The quality reporter must always test a MIDI note that lands on a real
    // zone. A bare C4 probe can fall outside a root-only sample map (e.g. a
    // 36-zone piano whose zones are single-key roots), producing a false
    // NO_ZONE_FOR_TEST_NOTE. This picks the covering zone for the preferred
    // note, or the nearest zone root, and reports exactly what it chose.
    struct ReportZoneProbe
    {
        int          firstZoneRoot   = -1;
        int          lastZoneRoot    = -1;
        int          testMidiNote    = -1;
        int          selectedZoneRoot = -1;
        juce::String selectedZoneFile;
        bool         hasZone     = false;   // a real zone backs testMidiNote
        bool         usedNearest = false;   // had to fall back to nearest root
    };

    ReportZoneProbe probeReportZone(int preferredMidi = 60, int velocity = 100) const noexcept
    {
        ReportZoneProbe r;
        if (activeMultisample == nullptr || activeMultisample->zones.empty())
            return r;

        const auto& zones = activeMultisample->zones;
        int minRoot = 1000, maxRoot = -1;
        for (const auto& z : zones)
        {
            minRoot = juce::jmin(minRoot, z.rootMidi);
            maxRoot = juce::jmax(maxRoot, z.rootMidi);
        }
        r.firstZoneRoot = minRoot;
        r.lastZoneRoot  = maxRoot;

        // 1) Prefer a zone whose hard key-range covers the preferred note,
        //    clamped into the actual zone span first.
        const int test = juce::jlimit(minRoot, maxRoot, preferredMidi);
        for (const auto& z : zones)
            if (test >= z.lowKey && test <= z.highKey
                && velocity >= z.loVel && velocity <= z.hiVel)
            {
                r.testMidiNote = test; r.selectedZoneRoot = z.rootMidi;
                r.selectedZoneFile = z.fileName; r.hasZone = true;
                return r;
            }
        for (const auto& z : zones)
            if (test >= z.lowKey && test <= z.highKey)
            {
                r.testMidiNote = test; r.selectedZoneRoot = z.rootMidi;
                r.selectedZoneFile = z.fileName; r.hasZone = true;
                return r;
            }

        // 2) No covering zone — test the nearest zone ROOT so the probe always
        //    lands on a real sample.
        const dida::SampleZone* nearest = &zones.front();
        int bestDist = 1 << 30;
        for (const auto& z : zones)
        {
            const int d = std::abs(z.rootMidi - preferredMidi);
            if (d < bestDist) { bestDist = d; nearest = &z; }
        }
        r.testMidiNote = nearest->rootMidi;
        r.selectedZoneRoot = nearest->rootMidi;
        r.selectedZoneFile = nearest->fileName;
        r.hasZone = true;
        r.usedNearest = true;
        return r;
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
