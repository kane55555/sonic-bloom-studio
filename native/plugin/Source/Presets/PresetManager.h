#pragma once
#include <JuceHeader.h>
#include <functional>
#include <vector>
#include <string>

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
};

class PresetManager
{
public:
    PresetManager(juce::AudioProcessor& proc);

    void scanPresetDirectory();
    void loadPreset(int index);
    void loadPresetFromFile(const juce::File& file);
    void saveCurrentPreset(const juce::String& name, const juce::String& category);
    void exportPreset(const juce::File& destination);

    int getNumPresets() const { return static_cast<int>(presets.size()); }
    int getCurrentPresetIndex() const { return currentIndex; }
    juce::String getPresetName(int index) const;

    std::function<void()> onPresetLoaded;

    // Most recent instrument folder requested by a preset (empty if none).
    const juce::String& getRequestedInstrument() const noexcept { return requestedInstrument; }

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

    void loadFactoryPresets();
    void loadUserPresets();
};
