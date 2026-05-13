#pragma once
//==============================================================================
//  PresetBrowser.h — Zenology-style collapsible category preset browser.
//
//  The browser receives a flat list of (name, category) pairs from the editor
//  and re-organises them into broad instrument categories with collapsible
//  sections. Categories are auto-derived from preset name keywords (with the
//  PresetManager-provided category as a fallback hint), so newly imported
//  one-shots like "Guitar 1", "Pad 2", "Choir 3" automatically land in the
//  right group without the UI being rewritten.
//
//  Public API is unchanged so PluginEditor wiring keeps working:
//    setPresets(names, categories)
//    onPresetSelected(globalIndex)
//==============================================================================
#include <JuceHeader.h>
#include <set>
#include <algorithm>
#include "Theme.h"

class PresetBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    PresetBrowser()
    {
        addAndMakeVisible(searchBox);
        searchBox.setTextToShowWhenEmpty("Search presets...", Theme::getColors().textSecondary);
        searchBox.setColour(juce::TextEditor::backgroundColourId, Theme::getColors().surfaceElevated);
        searchBox.setColour(juce::TextEditor::textColourId, Theme::getColors().textPrimary);
        searchBox.setColour(juce::TextEditor::outlineColourId, Theme::getColors().border);
        searchBox.onTextChange = [this]() { rebuildRows(); };

        addAndMakeVisible(list);
        list.setModel(this);
        list.setRowHeight(28);
        list.setColour(juce::ListBox::backgroundColourId, Theme::getColors().background);
    }

    /** Editor calls this with one entry per preset (parallel arrays). */
    void setPresets(const juce::StringArray& names, const juce::StringArray& categories)
    {
        items.clear();
        for (int i = 0; i < names.size(); ++i)
        {
            Item it;
            it.globalIndex = i;
            it.name        = names[i];
            it.hintCat     = i < categories.size() ? categories[i] : juce::String();
            it.category    = classify(it.name, it.hintCat);
            items.add(it);
        }
        rebuildRows();
    }

    /** Highlight a preset by its global index (called when a preset is loaded). */
    void setSelectedPreset(int globalIndex)
    {
        selectedGlobal = globalIndex;
        for (int r = 0; r < rows.size(); ++r)
            if (rows[r].kind == Row::Preset && rows[r].globalIndex == globalIndex)
            {
                // Make sure its category is open and the row is visible.
                openCategories.insert(rows[r].category);
                rebuildRows();
                list.selectRow(rowIndexForPreset(globalIndex));
                return;
            }
        list.repaint();
    }

    std::function<void(int)> onPresetSelected;

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        searchBox.setBounds(area.removeFromTop(30));
        area.removeFromTop(8);
        list.setBounds(area);
    }

    void paint(juce::Graphics& g) override { g.fillAll(Theme::getColors().background); }

private:
    //--------------------------------------------------------------------------
    // Data model
    struct Item
    {
        int    globalIndex = 0;
        juce::String name;
        juce::String hintCat;
        juce::String category;
    };

    struct Row
    {
        enum Kind { Header, Preset } kind = Preset;
        juce::String category;          // for Header & Preset
        juce::String label;
        int globalIndex = -1;           // Preset only
        int countInCategory = 0;        // Header only
    };

    //--------------------------------------------------------------------------
    // Categories (broad, Zenology-style). Order defines display order.
    static const juce::StringArray& categoryOrder()
    {
        static const juce::StringArray k {
            "All Sounds",
            "Pianos", "Keys", "Guitars", "Strings", "Pads", "Bells", "Plucks",
            "Leads", "Bass", "Synths", "Choirs", "Brass", "Winds",
            "Drums", "FX", "Imported"
        };
        return k;
    }

    /** Map a preset to one of the broad categories. The folder hint wins
        whenever it matches a known bucket — that way dropping a file into
        Presets/User/Pads always lands in "Pads" regardless of its filename. */
    static juce::String classify(const juce::String& nameIn, const juce::String& hintCat)
    {
        // 1) Folder hint takes priority.
        for (auto& c : categoryOrder())
            if (c.equalsIgnoreCase(hintCat)) return c;

        // 2) Otherwise infer from the preset name.
        const auto n = nameIn.toLowerCase();
        auto any = [&](std::initializer_list<const char*> kws) {
            for (auto* k : kws) if (n.contains(k)) return true;
            return false;
        };
        if (any({"piano", "grand", "upright", "rhodes", "wurli"}))      return "Pianos";
        if (any({"guitar", "nylon", "strat", "tele", "acoustic"}))      return "Guitars";
        if (any({"choir", "vocal", "vox", "ahh", "ooh", "voice"}))      return "Choirs";
        if (any({"violin", "viola", "cello", "orchestra", "string"}))   return "Strings";
        if (any({"brass", "trumpet", "trombone", "horn", "tuba", "sax"})) return "Brass";
        if (any({"flute", "clarinet", "oboe", "bassoon", "wind"}))      return "Winds";
        if (any({"bell", "mallet", "music box", "chime", "glock"}))     return "Bells";
        if (any({"pluck"}))                                              return "Plucks";
        if (any({"pad", "ambient", "atmosphere", "drone"}))              return "Pads";
        if (any({"lead", "solo"}))                                       return "Leads";
        if (any({"bass", "808", "sub"}))                                 return "Bass";
        if (any({"drum", "kick", "snare", "clap", "hat", "perc", "tom", "cymbal"}))
                                                                          return "Drums";
        if (any({"fx", "texture", "noise", "sweep", "riser", "impact", "hit"}))
                                                                          return "FX";
        if (any({"key", "ep", "organ", "clav"}))                         return "Keys";
        if (any({"synth"}))                                              return "Synths";

        return "Imported";
    }

    //--------------------------------------------------------------------------
    void rebuildRows()
    {
        const auto query = searchBox.getText().trim().toLowerCase();
        rows.clear();

        // Group items by category.
        juce::HashMap<juce::String, juce::Array<const Item*>> byCat;
        for (auto& it : items)
        {
            if (query.isNotEmpty() && ! it.name.toLowerCase().contains(query)) continue;
            if (! byCat.contains(it.category)) byCat.set(it.category, {});
            byCat.getReference(it.category).add(&it);
        }

        // "All Sounds" pseudo-category (always present at top).
        {
            Row h; h.kind = Row::Header; h.category = "All Sounds";
            int total = 0;
            for (auto& it : items)
                if (query.isEmpty() || it.name.toLowerCase().contains(query)) ++total;
            h.label = "All Sounds"; h.countInCategory = total;
            rows.add(h);

            const bool open = openCategories.find("All Sounds") != openCategories.end()
                              || query.isNotEmpty();
            if (open)
            {
                juce::Array<const Item*> all;
                for (auto& it : items)
                    if (query.isEmpty() || it.name.toLowerCase().contains(query)) all.add(&it);
                std::sort(all.begin(), all.end(), [](const Item* a, const Item* b) {
                    return a->name.compareNatural(b->name) < 0;
                });
                for (auto* it : all)
                {
                    Row r; r.kind = Row::Preset; r.category = "All Sounds";
                    r.label = it->name; r.globalIndex = it->globalIndex;
                    rows.add(r);
                }
            }
        }

        // Real categories in defined order.
        for (auto& cat : categoryOrder())
        {
            if (cat == "All Sounds") continue;
            if (! byCat.contains(cat)) continue;
            auto& list = byCat.getReference(cat);
            if (list.isEmpty()) continue;

            Row h; h.kind = Row::Header; h.category = cat;
            h.label = cat; h.countInCategory = list.size();
            rows.add(h);

            const bool open = openCategories.find(cat) != openCategories.end()
                              || query.isNotEmpty();
            if (open)
            {
                std::sort(list.begin(), list.end(), [](const Item* a, const Item* b) {
                    return a->name.compareNatural(b->name) < 0;
                });
                for (auto* it : list)
                {
                    Row r; r.kind = Row::Preset; r.category = cat;
                    r.label = it->name; r.globalIndex = it->globalIndex;
                    rows.add(r);
                }
            }
        }

        list.updateContent();
        list.repaint();
    }

    int rowIndexForPreset(int globalIndex) const
    {
        for (int i = 0; i < rows.size(); ++i)
            if (rows[i].kind == Row::Preset && rows[i].globalIndex == globalIndex) return i;
        return -1;
    }

    //--------------------------------------------------------------------------
    // ListBoxModel
    int getNumRows() override { return rows.size(); }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool /*sel*/) override
    {
        if (rowNumber < 0 || rowNumber >= rows.size()) return;
        auto& C = Theme::getColors();
        const auto& r = rows[rowNumber];

        if (r.kind == Row::Header)
        {
            g.fillAll(C.surfaceElevated);
            g.setColour(C.border);
            g.drawHorizontalLine(height - 1, 0.0f, (float) width);

            const bool open = openCategories.find(r.category) != openCategories.end()
                              || searchBox.getText().isNotEmpty();
            g.setColour(C.accentTeal);
            juce::Path tri;
            const float cx = 14.0f, cy = height * 0.5f;
            if (open) { tri.addTriangle(cx - 4, cy - 3, cx + 4, cy - 3, cx, cy + 4); }
            else      { tri.addTriangle(cx - 3, cy - 4, cx - 3, cy + 4, cx + 4, cy); }
            g.fillPath(tri);

            g.setColour(C.textPrimary);
            g.setFont(Theme::getHeadingFont(13.0f));
            g.drawText(r.label, 28, 0, width - 80, height, juce::Justification::centredLeft);

            g.setColour(C.textSecondary);
            g.setFont(Theme::getBodyFont(11.0f));
            g.drawText(juce::String(r.countInCategory), 0, 0, width - 12, height,
                       juce::Justification::centredRight);
        }
        else
        {
            const bool isSelected = (r.globalIndex == selectedGlobal);
            if (isSelected)
            {
                g.setColour(C.accentTeal.withAlpha(0.18f));
                g.fillRect(0, 0, width, height);
                g.setColour(C.accentTeal);
                g.fillRect(0, 0, 3, height);
            }
            g.setColour(isSelected ? C.accentTeal : C.textPrimary);
            g.setFont(Theme::getBodyFont(13.0f));
            g.drawText(r.label, 36, 0, width - 48, height, juce::Justification::centredLeft);
        }
    }

    void listBoxItemClicked(int rowNumber, const juce::MouseEvent&) override
    {
        if (rowNumber < 0 || rowNumber >= rows.size()) return;
        const auto r = rows[rowNumber];
        if (r.kind == Row::Header)
        {
            if (openCategories.count(r.category)) openCategories.erase(r.category);
            else openCategories.insert(r.category);
            rebuildRows();
        }
        else
        {
            selectedGlobal = r.globalIndex;
            list.repaint();
            if (onPresetSelected) onPresetSelected(r.globalIndex);
        }
    }

    //--------------------------------------------------------------------------
    juce::TextEditor searchBox;
    juce::ListBox    list;
    juce::Array<Item> items;
    juce::Array<Row>  rows;
    std::set<juce::String> openCategories;
    int selectedGlobal = -1;
};
