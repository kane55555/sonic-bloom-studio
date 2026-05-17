#pragma once
#include <JuceHeader.h>
#include <functional>
#include <vector>
#include <string>
#include "MacroMapper.h"

struct PresetInfo
{
    juce::String name;
    juce::String author;
    juce::String category;
    juce::StringArray tags;
    juce::String description;
    juce::String filePath;
    bool isFavorite = false;
    bool isFactory = false;

    // "Drop folder" presets: a raw audio one-shot living inside one of the
    // category subfolders under Samples/Presets/User/<Category>/. When loaded, the
    // engine swaps to this sample directly — no .didasynthpreset required.
    bool isSampleDrop = false;
    juce::String sampleSourcePath; // absolute path to the audio file (representative)
    int  sampleRootMidi = 60;
    bool sampleLooping = false;
    // When the drop is a folder of per-note samples ("Guitars/Guitar 1/_C3.wav"
    // ..."_A5.wav"), this holds every file in the group so the engine can
    // build a true multisampled instrument stretched across the keyboard.
    juce::StringArray sampleSourcePaths;
    juce::String sampleFolderPath;

    // ".diapreset" user preset (JSON + multisample folder reference). When
    // set, the preset's parameter snapshot is applied on load and the
    // referenced folder is routed into the engine as a multisample.
    bool isUserPreset = false;
    juce::String userPresetFile; // .diapreset path on disk
};

namespace dida { namespace preset {
    // Broad instrument categories. Each becomes a folder under
    //   <Documents>/DIDITAGAIN STUDIO/Samples/Presets/User/<Category>/
    // Drop a .wav/.aif/.flac/.mp3/.ogg one-shot in there and it shows up
    // automatically in the Browser tab as "<Category> N".
    inline const juce::StringArray& dropCategories()
    {
        static const juce::StringArray k {
            "Pianos", "Keys", "Guitars", "Strings", "Pads", "Bells", "Plucks",
            "Leads", "Bass", "Synths", "Choirs", "Brass", "Winds",
            "Drums", "FX", "Imported"
        };
        return k;
    }
}}

class PresetManager
{
public:
    PresetManager(juce::AudioProcessor& proc);

    void scanPresetDirectory();
    void loadPreset(int index);
    void loadPresetFromFile(const juce::File& file);
    int findPresetIndexByFile(const juce::File& file) const;
    void saveCurrentPreset(const juce::String& name, const juce::String& category);
    void exportPreset(const juce::File& destination);

    int getNumPresets() const { return static_cast<int>(presets.size()); }
    int getCurrentPresetIndex() const { return currentIndex; }
    juce::String getPresetName(int index) const;
    juce::String getPresetCategory(int index) const
    {
        if (index >= 0 && index < (int) presets.size()) return presets[index].category;
        return {};
    }
    bool getPresetIsFactory(int index) const
    {
        if (index >= 0 && index < (int) presets.size()) return presets[index].isFactory;
        return true;
    }

    std::function<void()> onPresetLoaded;

    // Most recent instrument folder requested by a preset (empty if none).
    const juce::String& getRequestedInstrument() const noexcept { return requestedInstrument; }
    const juce::String& getRequestedSampleSource() const noexcept { return requestedSampleSource; }
    const juce::StringArray& getRequestedSampleSources() const noexcept { return requestedSampleSources; }
    const juce::String& getRequestedSampleFolderPath() const noexcept { return requestedSampleFolderPath; }
    int getRequestedSampleRootMidi() const noexcept { return requestedSampleRootMidi; }
    const juce::String& getRequestedSampleDisplayName() const noexcept { return requestedSampleDisplayName; }
    bool isCurrentPresetSampleSourceDriven() const noexcept { return requestedSampleSource.isNotEmpty(); }

    // V2 preset extras.
    bool getRequestedSampleLooping() const noexcept { return requestedSampleLooping; }
    const juce::String& getRequestedCategory() const noexcept { return requestedCategory; }
    dida::preset::MacroMapper& getMacroMapper() noexcept { return macroMapper; }

    void toggleFavorite(int index);
    std::vector<int> searchByTag(const juce::String& tag) const;
    std::vector<int> searchByName(const juce::String& query) const;

    juce::File getFactoryPresetDirectory() const;
    juce::File getUserPresetDirectory() const;

    static bool validatePresetFile(const juce::File& file);
    static juce::String computeChecksum(const juce::String& jsonContent);

private:
    juce::AudioProcessor& processor;
    std::vector<PresetInfo> presets;
    int currentIndex = 0;
    juce::String requestedInstrument;
    juce::String requestedSampleSource;
    juce::StringArray requestedSampleSources;
    juce::String requestedSampleFolderPath;
    juce::String requestedSampleDisplayName;
    int requestedSampleRootMidi = 60;
    bool requestedSampleLooping = false;
    juce::String requestedCategory;
    dida::preset::MacroMapper macroMapper;

    void loadFactoryPresets();
    void loadUserPresets();
    void loadDroppedSamples();
    void loadDiapresetFiles();
    void seedGuitarPresetBankIfMissing();
};
