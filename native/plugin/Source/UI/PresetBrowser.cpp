#include "PresetBrowser.h"
#include "Theme.h"
#include "BrowserComponent.h"

PresetBrowser::PresetBrowser()
{
    addAndMakeVisible(categoryBox);
    categoryBox.addItem("All",   1);
    categoryBox.addItem("Bass",  2);
    categoryBox.addItem("Keys",  3);
    categoryBox.addItem("Lead",  4);
    categoryBox.addItem("Pad",   5);
    categoryBox.addItem("Pluck", 6);
    categoryBox.addItem("FX",    7);
    categoryBox.setSelectedId(1, juce::dontSendNotification);
    categoryBox.onChange = [this]() { applyFilter(); };

    browser = std::make_unique<BrowserComponent>();
    addAndMakeVisible(*browser);
    browser->onPresetSelected = [this](int row)
    {
        if (onPresetSelected && row >= 0 && row < allIndices.size())
            onPresetSelected(allIndices[row]);
    };
}

PresetBrowser::~PresetBrowser() = default;

void PresetBrowser::setPresets(const juce::StringArray& names, const juce::StringArray& categories)
{
    allNames      = names;
    allCategories = categories;
    applyFilter();
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds().reduced(8);
    categoryBox.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);
    if (browser) browser->setBounds(area);
}

void PresetBrowser::paint(juce::Graphics& g)
{
    g.fillAll(Theme::getColors().background);
}

void PresetBrowser::applyFilter()
{
    const auto cat = categoryBox.getText();
    juce::StringArray filtered;
    allIndices.clear();
    const int n = allNames.size();
    for (int i = 0; i < n; ++i)
    {
        const bool match = (cat == "All")
                        || (i < allCategories.size() && allCategories[i].equalsIgnoreCase(cat));
        if (match)
        {
            filtered.add(allNames[i]);
            allIndices.add(i);
        }
    }
    if (browser) browser->setPresetNames(filtered);
}
