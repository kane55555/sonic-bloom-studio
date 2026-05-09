#pragma once
//==============================================================================
//  PresetBrowser.h — Categorised preset browser with search + tag filter.
//
//  Lightweight header: pulls in only juce_gui_basics, no JuceHeader.h, no
//  audio-processor headers. Implementation lives in PresetBrowser.cpp.
//==============================================================================
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

class BrowserComponent;

class PresetBrowser : public juce::Component
{
public:
    PresetBrowser();
    ~PresetBrowser() override;

    void setPresets(const juce::StringArray& names, const juce::StringArray& categories);

    std::function<void(int)> onPresetSelected;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void applyFilter();

    juce::ComboBox categoryBox;
    std::unique_ptr<BrowserComponent> browser;
    juce::StringArray allNames, allCategories;
    juce::Array<int>  allIndices;
};
