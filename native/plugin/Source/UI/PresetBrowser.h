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
        list.onKeyPressed = [this](const juce::KeyPress& key) { return keyPressed(key); };
        list.setRowHeight(28);
        list.setColour(juce::ListBox::backgroundColourId, Theme::getColors().background);

        // The ListBox must want keyboard focus so arrow keys keep stepping
        // presets after a row click. The search field remains focusable only
        // through a direct click; no preset/category action grabs it.
        list.setWantsKeyboardFocus(true);
        searchBox.setWantsKeyboardFocus(true);
        searchBox.setMouseClickGrabsKeyboardFocus(true);
        setWantsKeyboardFocus(false);
    }

    /** Allow the editor to intercept arrow keys regardless of which child has focus. */
    void attachKeyListener(juce::KeyListener* l)
    {
        addKeyListener(l);
        list.addKeyListener(l);
        searchBox.addKeyListener(l);
    }

    /** Editor calls this with one entry per preset (parallel arrays). */
    void setPresets(const juce::StringArray& names, const juce::StringArray& categories)
    {
        juce::Array<int> presetIndices;
        for (int i = 0; i < names.size(); ++i)
            presetIndices.add(i);
        setPresets(names, categories, presetIndices);
    }

    /** Same as above, but keeps the real PresetManager index when the editor
        hides factory presets from the displayed list. */
    void setPresets(const juce::StringArray& names, const juce::StringArray& categories,
                    const juce::Array<int>& presetIndices)
    {
        items.clear();
        for (int i = 0; i < names.size(); ++i)
        {
            Item it;
            it.globalIndex = i < presetIndices.size() ? presetIndices[i] : i;
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

    /** Move selection to the next/previous preset row (skips category headers).
        Opens collapsed categories as needed and fires onPresetSelected. */
    bool stepSelection(int delta)
    {
        if (rows.isEmpty() || delta == 0) return false;

        // Build a flat list of all presets in display order, opening every
        // category so arrow-key navigation can flow across category boundaries
        // even when sections are collapsed.
        bool anyClosed = false;
        for (auto& cat : categoryOrder())
            if (openCategories.find(cat) == openCategories.end()) { openCategories.insert(cat); anyClosed = true; }
        if (anyClosed) rebuildRows();

        juce::Array<int> presetRowIdx;
        for (int i = 0; i < rows.size(); ++i)
            if (rows[i].kind == Row::Preset && rows[i].category == "All Sounds")
                presetRowIdx.add(i);
        if (presetRowIdx.isEmpty()) return false;

        int curPos = -1;
        for (int i = 0; i < presetRowIdx.size(); ++i)
            if (rows[presetRowIdx[i]].globalIndex == selectedGlobal) { curPos = i; break; }

        int next = (curPos < 0) ? (delta > 0 ? 0 : presetRowIdx.size() - 1)
                                : juce::jlimit(0, presetRowIdx.size() - 1, curPos + delta);
        const int rowIdx = presetRowIdx[next];
        selectedGlobal = rows[rowIdx].globalIndex;
        list.selectRow(rowIdx);
        list.scrollToEnsureRowIsOnscreen(rowIdx);
        list.repaint();
        if (onPresetSelected) onPresetSelected(selectedGlobal);
        return true;
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::spaceKey) return false;

        int delta = 0;
        if      (key == juce::KeyPress::downKey  || key == juce::KeyPress::rightKey) delta =  1;
        else if (key == juce::KeyPress::upKey    || key == juce::KeyPress::leftKey)  delta = -1;
        else if (key == juce::KeyPress::pageDownKey) delta =  10;
        else if (key == juce::KeyPress::pageUpKey)   delta = -10;
        else return false;

        return stepSelection(delta);
    }

    bool keyStateChanged(bool /*isKeyDown*/) override { return false; }

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

    /** Map a preset to a category. The folder hint always wins so the browser
        mirrors the user's folder structure under Samples/Presets/User/ exactly
        as named — no consolidation of similar-sounding folders. */
    static juce::String classify(const juce::String& nameIn, const juce::String& hintCat)
    {
        // Folder hint wins verbatim — preserves the user's exact folder name
        // so "Guitars" and "Electric Guitars" stay as separate tabs.
        if (hintCat.trim().isNotEmpty()) return hintCat.trim();

        // No folder hint: infer a bucket from the preset name as a fallback.
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

        // Custom user folders: any category not in the built-in order gets
        // appended at the bottom, displayed with its folder name as typed.
        {
            juce::StringArray customCats;
            for (juce::HashMap<juce::String, juce::Array<const Item*>>::Iterator it(byCat); it.next();)
            {
                const auto& cat = it.getKey();
                if (categoryOrder().contains(cat)) continue;
                customCats.add(cat);
            }
            customCats.sort(true);
            for (auto& cat : customCats)
            {
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
        // Preset/category clicks should keep keyboard focus on the browser list
        // so arrow stepping continues. Never move focus to the search box here.
        list.grabKeyboardFocus();
        const auto r = rows[rowNumber];
        if (r.kind == Row::Header)
        {
            list.selectRow(rowNumber);
            if (openCategories.count(r.category)) openCategories.erase(r.category);
            else openCategories.insert(r.category);
            rebuildRows();
        }
        else
        {
            selectedGlobal = r.globalIndex;
            list.selectRow(rowNumber);
            list.repaint();
            if (onPresetSelected) onPresetSelected(r.globalIndex);
        }
    }

    //--------------------------------------------------------------------------
    // ListBox subclass that refuses to swallow the spacebar so the DAW still
    // receives transport play/pause while the preset list has keyboard focus.
    struct TransportFriendlyListBox : public juce::ListBox
    {
        bool keyPressed(const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::spaceKey) return false;
            if (onKeyPressed && onKeyPressed(k)) return true;
            return juce::ListBox::keyPressed(k);
        }

        bool keyStateChanged(bool /*isKeyDown*/) override
        {
            // Do not consume raw key-state changes. In FL Studio this is
            // important for allowing transport shortcuts (spacebar) to stay
            // host-owned while the list has focus for arrow browsing.
            return false;
        }

        std::function<bool(const juce::KeyPress&)> onKeyPressed;
    };

    juce::TextEditor searchBox;
    TransportFriendlyListBox list;
    juce::Array<Item> items;
    juce::Array<Row>  rows;
    std::set<juce::String> openCategories;
    int selectedGlobal = -1;
};
