#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class DiditagainEditor : public juce::AudioProcessorEditor
{
public:
    explicit DiditagainEditor(DiditagainProcessor& p);
    ~DiditagainEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    DiditagainProcessor& processor;

    // Tab navigation
    enum class Tab { Browser, Synth, Mod, FX, Settings, Account };
    Tab currentTab = Tab::Synth;

    juce::TextButton tabBrowser{"BROWSER"};
    juce::TextButton tabSynth{"SYNTH"};
    juce::TextButton tabMod{"MOD"};
    juce::TextButton tabFX{"FX"};
    juce::TextButton tabSettings{"SETTINGS"};
    juce::TextButton tabAccount{"ACCOUNT"};

    // Preset browser bar
    juce::ComboBox presetSelector;
    juce::TextButton prevPreset{"<"};
    juce::TextButton nextPreset{">"};
    juce::TextButton savePreset{"SAVE"};

    void setupTabs();
    void switchTab(Tab tab);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiditagainEditor)
};
