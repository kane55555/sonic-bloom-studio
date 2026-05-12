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
        categoryBox.addItem("All", 1);
        categoryBox.setSelectedId(1, juce::dontSendNotification);
        categoryBox.onChange = [this]() { applyFilter(); };

        addAndMakeVisible(browser);
        browser.onPresetSelected = [this](int row) {
            if (onPresetSelected && row >= 0 && row < allIndices.size())
                onPresetSelected(allIndices[row]);
        };
    }

    void setPresets(const juce::StringArray& names, const juce::StringArray& categories)
    {
        allNames = names;
        allCategories = categories;

        // Rebuild the category dropdown from whatever the data actually contains,
        // preserving the user's current selection if possible.
        const auto previous = categoryBox.getText();
        categoryBox.clear(juce::dontSendNotification);
        categoryBox.addItem("All", 1);
        juce::StringArray uniques;
        for (auto& c : categories)
            if (c.isNotEmpty() && ! uniques.contains(c, true)) uniques.add(c);
        uniques.sortNatural();
        int id = 2;
        for (auto& c : uniques) categoryBox.addItem(c, id++);
        int restoreId = 1;
        for (int i = 0; i < categoryBox.getNumItems(); ++i)
            if (categoryBox.getItemText(i).equalsIgnoreCase(previous))
                restoreId = categoryBox.getItemId(i);
        categoryBox.setSelectedId(restoreId, juce::dontSendNotification);

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
