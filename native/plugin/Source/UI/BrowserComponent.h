#pragma once
#include <JuceHeader.h>
#include "Theme.h"

class BrowserComponent : public juce::Component, public juce::ListBoxModel
{
public:
    BrowserComponent()
    {
        addAndMakeVisible(searchBox);
        searchBox.setTextToShowWhenEmpty("Search presets...", Theme::getColors().textSecondary);
        searchBox.onTextChange = [this]() { filterPresets(); };

        addAndMakeVisible(presetList);
        presetList.setModel(this);
        presetList.setRowHeight(36);
    }

    void setPresetNames(const juce::StringArray& names)
    {
        allPresets = names;
        filteredPresets = names;
        presetList.updateContent();
    }

    std::function<void(int)> onPresetSelected;

    // ListBoxModel
    int getNumRows() override { return filteredPresets.size(); }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool isSelected) override
    {
        auto& colors = Theme::getColors();
        if (isSelected)
            g.fillAll(colors.accentPurple.withAlpha(0.2f));

        g.setColour(colors.textPrimary);
        g.setFont(Theme::getBodyFont());
        g.drawText(filteredPresets[rowNumber], 12, 0, width - 24, height, juce::Justification::centredLeft);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent&) override
    {
        if (onPresetSelected) onPresetSelected(row);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        searchBox.setBounds(area.removeFromTop(32));
        area.removeFromTop(8);
        presetList.setBounds(area);
    }

private:
    void filterPresets()
    {
        auto query = searchBox.getText().toLowerCase();
        filteredPresets.clear();
        for (auto& p : allPresets)
            if (query.isEmpty() || p.toLowerCase().contains(query))
                filteredPresets.add(p);
        presetList.updateContent();
    }

    juce::TextEditor searchBox;
    juce::ListBox presetList;
    juce::StringArray allPresets;
    juce::StringArray filteredPresets;
};
