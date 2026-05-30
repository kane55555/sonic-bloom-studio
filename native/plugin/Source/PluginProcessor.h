#pragma once
#include <JuceHeader.h>
#include <atomic>
#include "DSP/SynthEngine.h"
#include "Presets/PresetManager.h"
#include "Licensing/LicenseClient.h"

class DiditagainProcessor : public juce::AudioProcessor
{
public:
    DiditagainProcessor();
    ~DiditagainProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    SynthEngine& getSynthEngine() { return synthEngine; }
    PresetManager& getPresetManager() { return presetManager; }
    LicenseClient& getLicenseClient() { return licenseClient; }

    // --- Debug introspection (used by PresetCycleTester) ---
    bool   getAppliedMonoMode() const noexcept   { return appliedMonoMode; }
    int    getAppliedPolyphony() const noexcept  { return appliedPolyphony; }
    bool   isPresetChangeQueued() const noexcept { return deferredPresetChange.queued; }
    int    getObservedPresetSerial() const noexcept { return observedPresetLoadSerial; }
    int    getRequestedPresetSerial() const noexcept { return presetLoadSerial.load(std::memory_order_acquire); }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    SynthEngine synthEngine;
    PresetManager presetManager;
    LicenseClient licenseClient;
    std::atomic<bool> presetLoadRequested { false };
    std::atomic<int> presetLoadSerial { 0 };
    int observedPresetLoadSerial = 0;

    struct DeferredPresetChange
    {
        bool queued = false;
        bool resetState = false;
        bool monoMode = false;
        int polyphony = 8;
        int presetSerial = 0;
        int ageInBlocks = 0;
    };

    DeferredPresetChange deferredPresetChange;
    bool appliedMonoMode = false;
    int appliedPolyphony = SynthEngine::MAX_POLYPHONY;
    int debugBlockCounter = 0;
    int clipBlocksSinceLog = 0;
    int stoppedBlocks = 0; // transport-stop counter for FX-tail flush

    // ---- Fresh-instance startup default + DAW project recall ----
    // hasRestoredState becomes true only when setStateInformation restored a
    // real saved project, so we never override a user's saved instrument with
    // the default piano. startupDefaultApplied guards the one-shot default load.
    bool hasRestoredState = false;
    bool startupDefaultApplied = false;

    // Selection restored from project state, replayed once the preset library
    // has scanned (in the audio thread before first render).
    struct RestoredSelection
    {
        bool pending = false;
        int  presetIndex = -1;
        juce::String presetName;
        juce::String presetCategory;
        juce::String presetFilePath;
        juce::String userPresetFilePath;
        bool fallbackSynthesisEnabled = false;
    };
    RestoredSelection restoredSelection;

    void applyStartupDefaultIfNeeded();


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiditagainProcessor)
};
