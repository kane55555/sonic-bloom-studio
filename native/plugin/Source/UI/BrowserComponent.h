#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <functional>

class BrowserComponent : public juce::Component, public juce::ListBoxModel
{
public:
    BrowserComponent();

    void setPresetNames(const juce::StringArray& names);

    std::function<void(int)> onPresetSelected;

    // ListBoxModel
    int getNumRows() override;

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool isSelected) override;

    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

    void resized() override;

private:
    void filterPresets();

    juce::TextEditor searchBox;
    juce::ListBox presetList;
    juce::StringArray allPresets;
    juce::StringArray filteredPresets;
};
