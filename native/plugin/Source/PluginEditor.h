#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/MainSynthPanel.h"
#include "UI/LayerEditor.h"
#include "UI/MacroPanel.h"
#include "UI/ModPanel.h"
#include "UI/FxPanel.h"
#include "UI/ImportReviewPanel.h"
#include "UI/SettingsPanel.h"
#include "UI/AccountPanel.h"
#include "UI/PresetBrowser.h"
#include "Debug/PresetCycleTester.h"
#include "DSP/SampleLibrary.h"

class DiditagainEditor : public juce::AudioProcessorEditor
{
public:
    explicit DiditagainEditor(DiditagainProcessor& p);
    ~DiditagainEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    DiditagainProcessor& processor;

    enum class Tab { Browser, Layers, Synth, Mod, FX, Import, Settings, Account };
    Tab currentTab = Tab::Layers;
    bool advancedMode = true;

    juce::TextButton tabBrowser{"BROWSER"};
    juce::TextButton tabLayers{"LAYERS"};
    juce::TextButton tabSynth{"SYNTH"};
    juce::TextButton tabMod{"MOD"};
    juce::TextButton tabFX{"FX"};
    juce::TextButton tabImport{"IMPORT"};
    juce::TextButton tabSettings{"SETTINGS"};
    juce::TextButton tabAccount{"ACCOUNT"};
    juce::TextButton modeToggle{"ADVANCED"};

    juce::ComboBox presetSelector;
    juce::TextButton prevPreset{"<"};
    juce::TextButton nextPreset{">"};
    juce::TextButton savePreset{"SAVE"};
    juce::TextButton cycleTestButton{"CYCLE TEST"};
    juce::TextEditor cycleTestLog;
    std::unique_ptr<PresetCycleTester> cycleTester;

    // Real content panels.
    std::unique_ptr<LayerEditor>      layerPanel;
    std::unique_ptr<MacroPanel>       macroPanel;
    std::unique_ptr<MainSynthPanel>   synthPanel;
    std::unique_ptr<PresetBrowser>    presetBrowserPanel;
    std::unique_ptr<ModPanel>         modPanel;
    std::unique_ptr<FxPanel>          fxPanel;
    std::unique_ptr<ImportReviewPanel> importPanel;
    std::unique_ptr<SettingsPanel>    settingsPanel;
    std::unique_ptr<AccountPanel>     accountPanel;

    void setupTabs();
    void switchTab(Tab tab);
    void refreshPresetCombo();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiditagainEditor)
};
