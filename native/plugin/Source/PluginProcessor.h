#pragma once
#include <JuceHeader.h>
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

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    SynthEngine synthEngine;
    PresetManager presetManager;
    LicenseClient licenseClient;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiditagainProcessor)
};
