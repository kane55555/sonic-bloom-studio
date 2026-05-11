#pragma once
//==============================================================================
//  PresetIndex.h — Read/write Documents/DIDITAGAIN STUDIO/Presets/index.json,
//  the searchable index produced by import_samples.py and consumed by the
//  in-plugin preset browser.
//==============================================================================
#include <JuceHeader.h>
#include <vector>

namespace dida { namespace preset {

struct PresetIndexEntry
{
    juce::String presetId;
    juce::String name;
    juce::String bank;          // "Factory" | "User"
    juce::String category;
    juce::StringArray tags;
    juce::String presetPath;    // relative to studio root
    juce::String samplePath;    // relative to studio root, may be empty
    juce::String createdAt;
    juce::String modifiedAt;
    bool favorite = false;
    bool userEdited = false;
    bool needsReview = false;
};

class PresetIndex
{
public:
    /** Load index.json from the given studio root. Missing file = empty. */
    static std::vector<PresetIndexEntry> load(const juce::File& studioRoot);

    /** Atomically write the index back to studio root/Presets/index.json. */
    static bool save(const juce::File& studioRoot,
                     const std::vector<PresetIndexEntry>& entries);

    /** Replace any entry whose presetPath matches `entry.presetPath`,
        or append. */
    static void upsert(std::vector<PresetIndexEntry>& list,
                       const PresetIndexEntry& entry);

    static std::vector<PresetIndexEntry>
    filterByCategory(const std::vector<PresetIndexEntry>& list,
                     const juce::String& category);

    static std::vector<PresetIndexEntry>
    filterNeedsReview(const std::vector<PresetIndexEntry>& list);
};

}} // namespace dida::preset
