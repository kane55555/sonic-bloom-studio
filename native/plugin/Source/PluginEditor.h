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
#include "UI/AiTexturePanel.h"
#include "UI/PresetBrowser.h"
#include "UI/AudioCropPanel.h"
#include "DSP/SampleLibrary.h"

// Visible top-level tabs. Everything else lives in the header MENU dropdown.
class DiditagainEditor : public juce::AudioProcessorEditor,
                         public juce::KeyListener
{
public:
    explicit DiditagainEditor(DiditagainProcessor& p);
    ~DiditagainEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;

private:
    DiditagainProcessor& processor;

    enum class Tab { Browser, Layers, FX, AudioCrop };
    Tab currentTab = Tab::Browser;

    // Visible tabs only
    juce::TextButton tabBrowser  {"BROWSER"};
    juce::TextButton tabLayers   {"LAYERS"};
    juce::TextButton tabFX       {"FX"};
    juce::TextButton tabAudioCrop{"AUDIO CROP"};

    // Header
    juce::TextButton menuButton{ juce::String::charToString(juce::juce_wchar(0x2630)) }; // ☰
    juce::TextButton savePreset{"SAVE"};
    juce::TextButton directMonitorButton{"DIRECT"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> directMonitorAttach;

    // Embedded header logo (compiled into the VST3 via BinaryData).
    juce::Image logoImage;

    // Panels (Browser/Layers/FX always visible via tabs; rest opened modally)
    std::unique_ptr<LayerEditor>      layerPanel;
    std::unique_ptr<MacroPanel>       macroPanel;
    std::unique_ptr<MainSynthPanel>   synthPanel;
    std::unique_ptr<PresetBrowser>    presetBrowserPanel;
    std::unique_ptr<ModPanel>         modPanel;
    std::unique_ptr<FxPanel>          fxPanel;
    std::unique_ptr<ImportReviewPanel> importPanel;
    std::unique_ptr<SettingsPanel>    settingsPanel;
    std::unique_ptr<AccountPanel>     accountPanel;
    std::unique_ptr<AiTexturePanel>   aiTexturePanel;
    std::unique_ptr<AudioCropPanel>   audioCropPanel;

    // Modal overlay state
    juce::Component* overlayPanel = nullptr;
    juce::TextButton overlayClose{"CLOSE"};
    juce::Label      overlayTitle;

    void setupTabs();
    void switchTab(Tab tab);
    void refreshPresetCombo();
    void refreshBrowserPresets();
    void showOverlay(juce::Component* panel, const juce::String& title);
    void hideOverlay();
    void openMenu();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiditagainEditor)
};
