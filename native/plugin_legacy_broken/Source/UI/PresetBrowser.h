#pragma once
//==============================================================================
//  PresetBrowser.h — Categorised preset browser with search and tag filter.
//
//  Wraps the existing BrowserComponent with a category sidebar on the left
//  so users can scan presets by Bass / Keys / Lead / Pad / Pluck / FX.
//==============================================================================
#include <JuceHeader.h>
#include "Theme.h"
#include "BrowserComponent.h"

class PresetBrowser : public juce::Component
{
public:
    PresetBrowser()
    {
        addAndMakeVisible(categoryBox);
        categoryBox.addItem("All",     1);
        categoryBox.addItem("Bass",    2);
        categoryBox.addItem("Keys",    3);
        categoryBox.addItem("Lead",    4);
        categoryBox.addItem("Pad",     5);
        categoryBox.addItem("Pluck",   6);
        categoryBox.addItem("FX",      7);
        categoryBox.setSelectedId(1, juce::dontSendNotification);
        categoryBox.onChange = [this]() { applyFilter(); };

        addAndMakeVisible(browser);
        browser.onPresetSelected = [this](int row) { if (onPresetSelected) onPresetSelected(allIndices[row]); };
    }

    void setPresets(const juce::StringArray& names, const juce::StringArray& categories)
    {
        allNames = names;
        allCategories = categories;
        applyFilter();
    }

    std::function<void(int)> onPresetSelected;

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        categoryBox.setBounds(area.removeFromTop(28));
        area.removeFromTop(8);
        browser.setBounds(area);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::getColors().background);
    }

private:
    void applyFilter()
    {
        const auto cat = categoryBox.getText();
        juce::StringArray filtered;
        allIndices.clear();
        for (int i = 0; i < allNames.size(); ++i)
        {
            if (cat == "All" || allCategories[i].equalsIgnoreCase(cat))
            {
                filtered.add(allNames[i]);
                allIndices.add(i);
            }
        }
        browser.setPresetNames(filtered);
    }

    juce::ComboBox categoryBox;
    BrowserComponent browser;
    juce::StringArray allNames, allCategories;
    juce::Array<int> allIndices;
};
