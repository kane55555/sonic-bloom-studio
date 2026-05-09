#include "BrowserComponent.h"
#include "Theme.h"

BrowserComponent::BrowserComponent()
{
    addAndMakeVisible(searchBox);
    searchBox.setTextToShowWhenEmpty("Search presets...", Theme::getColors().textSecondary);
    searchBox.onTextChange = [this]() { filterPresets(); };

    addAndMakeVisible(presetList);
    presetList.setModel(this);
    presetList.setRowHeight(36);
}

void BrowserComponent::setPresetNames(const juce::StringArray& names)
{
    allPresets = names;
    filteredPresets = names;
    presetList.updateContent();
}

int BrowserComponent::getNumRows()
{
    return filteredPresets.size();
}

void BrowserComponent::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool isSelected)
{
    auto& colors = Theme::getColors();
    if (isSelected)
        g.fillAll(colors.accentPurple.withAlpha(0.2f));

    g.setColour(colors.textPrimary);
    g.setFont(Theme::getBodyFont());
    g.drawText(filteredPresets[rowNumber], 12, 0, width - 24, height, juce::Justification::centredLeft);
}

void BrowserComponent::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (onPresetSelected) onPresetSelected(row);
}

void BrowserComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    searchBox.setBounds(area.removeFromTop(32));
    area.removeFromTop(8);
    presetList.setBounds(area);
}

void BrowserComponent::filterPresets()
{
    auto query = searchBox.getText().toLowerCase();
    filteredPresets.clear();
    for (auto& p : allPresets)
        if (query.isEmpty() || p.toLowerCase().contains(query))
            filteredPresets.add(p);
    presetList.updateContent();
}
