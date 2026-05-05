#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/MainSynthPanel.h"
#include "UI/PresetBrowser.h"
#include "Debug/PresetCycleTester.h"

class DiditagainEditor : public juce::AudioProcessorEditor
{
public:
    explicit DiditagainEditor(DiditagainProcessor& p);
    ~DiditagainEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    DiditagainProcessor& processor;

    enum class Tab { Browser, Synth, Mod, FX, Settings, Account };
    Tab currentTab = Tab::Synth;

    juce::TextButton tabBrowser{"BROWSER"};
    juce::TextButton tabSynth{"SYNTH"};
    juce::TextButton tabMod{"MOD"};
    juce::TextButton tabFX{"FX"};
    juce::TextButton tabSettings{"SETTINGS"};
    juce::TextButton tabAccount{"ACCOUNT"};

    juce::ComboBox presetSelector;
    juce::TextButton prevPreset{"<"};
    juce::TextButton nextPreset{">"};
    juce::TextButton savePreset{"SAVE"};

    // Real content panels.
    std::unique_ptr<MainSynthPanel> synthPanel;
    std::unique_ptr<PresetBrowser>  presetBrowserPanel;
    juce::Component                  placeholderPanel; // for Mod/FX/Settings/Account fallback
    juce::Label                      placeholderLabel;

    void setupTabs();
    void switchTab(Tab tab);
    void refreshPresetCombo();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiditagainEditor)
};
