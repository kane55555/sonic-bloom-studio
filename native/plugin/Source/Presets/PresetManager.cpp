#include "PresetManager.h"

PresetManager::PresetManager(juce::AudioProcessor& proc) : processor(proc)
{
    scanPresetDirectory();
}

void PresetManager::scanPresetDirectory()
{
    presets.clear();
    loadFactoryPresets();
    loadUserPresets();
}

void PresetManager::loadFactoryPresets()
{
    auto dir = getFactoryPresetDirectory();
    if (!dir.isDirectory()) return;

    auto files = dir.findChildFiles(juce::File::findFiles, true, "*.didasynthpreset");
    for (auto& file : files)
    {
        auto json = juce::JSON::parse(file);
        if (json.isObject())
        {
            PresetInfo info;
            info.name = json.getProperty("presetName", "Untitled").toString();
            info.author = json.getProperty("author", "DIDITAGAIN").toString();
            info.category = json.getProperty("category", "Init").toString();
            info.description = json.getProperty("description", "").toString();
            info.filePath = file.getFullPathName();
            info.isFactory = true;

            auto tagsVar = json.getProperty("tags", juce::var());
            if (auto* tagsArray = tagsVar.getArray())
                for (auto& t : *tagsArray)
                    info.tags.add(t.toString());

            presets.push_back(info);
        }
    }
}

void PresetManager::loadUserPresets()
{
    auto dir = getUserPresetDirectory();
    if (!dir.isDirectory()) { dir.createDirectory(); return; }

    auto files = dir.findChildFiles(juce::File::findFiles, true, "*.didasynthpreset");
    for (auto& file : files)
    {
        auto json = juce::JSON::parse(file);
        if (json.isObject())
        {
            PresetInfo info;
            info.name = json.getProperty("presetName", "Untitled").toString();
            info.author = json.getProperty("author", "User").toString();
            info.category = json.getProperty("category", "User").toString();
            info.filePath = file.getFullPathName();
            info.isFactory = false;
            presets.push_back(info);
        }
    }
}

void PresetManager::loadPreset(int index)
{
    if (index < 0 || index >= static_cast<int>(presets.size())) return;
    currentIndex = index;

    juce::File file(presets[index].filePath);
    loadPresetFromFile(file);
}

void PresetManager::loadPresetFromFile(const juce::File& file)
{
    if (!validatePresetFile(file)) return;

    auto json = juce::JSON::parse(file);
    if (!json.isObject()) return;

    // TODO: Apply all parameter values from JSON to AudioProcessorValueTreeState
    // This maps each JSON key to the corresponding APVTS parameter
}

void PresetManager::saveCurrentPreset(const juce::String& name, const juce::String& category)
{
    auto dir = getUserPresetDirectory();
    dir.createDirectory();
    auto file = dir.getChildFile(name + ".didasynthpreset");

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("presetVersion", "1.0.0");
    obj->setProperty("presetName", name);
    obj->setProperty("author", "User");
    obj->setProperty("category", category);
    obj->setProperty("tags", juce::var());
    // TODO: Serialize all parameter values from APVTS

    auto jsonStr = juce::JSON::toString(juce::var(obj.get()));
    obj->setProperty("checksum", computeChecksum(jsonStr));

    file.replaceWithText(juce::JSON::toString(juce::var(obj.get())));
    scanPresetDirectory();
}

void PresetManager::exportPreset(const juce::File& destination)
{
    if (currentIndex >= 0 && currentIndex < static_cast<int>(presets.size()))
    {
        juce::File src(presets[currentIndex].filePath);
        src.copyFileTo(destination);
    }
}

juce::String PresetManager::getPresetName(int index) const
{
    if (index >= 0 && index < static_cast<int>(presets.size()))
        return presets[index].name;
    return "Init";
}

void PresetManager::toggleFavorite(int index)
{
    if (index >= 0 && index < static_cast<int>(presets.size()))
        presets[index].isFavorite = !presets[index].isFavorite;
}

std::vector<int> PresetManager::searchByTag(const juce::String& tag) const
{
    std::vector<int> results;
    for (int i = 0; i < static_cast<int>(presets.size()); ++i)
        if (presets[i].tags.contains(tag, true))
            results.push_back(i);
    return results;
}

std::vector<int> PresetManager::searchByName(const juce::String& query) const
{
    std::vector<int> results;
    for (int i = 0; i < static_cast<int>(presets.size()); ++i)
        if (presets[i].name.containsIgnoreCase(query))
            results.push_back(i);
    return results;
}

juce::File PresetManager::getFactoryPresetDirectory() const
{
#if JUCE_WINDOWS
    return juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory)
        .getChildFile("DIDITAGAIN").getChildFile("DIDITAGAIN STUDIO").getChildFile("Presets").getChildFile("Factory");
#else
    return juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory)
        .getChildFile("DIDITAGAIN").getChildFile("DIDITAGAIN STUDIO").getChildFile("Presets").getChildFile("Factory");
#endif
}

juce::File PresetManager::getUserPresetDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DIDITAGAIN").getChildFile("DIDITAGAIN STUDIO").getChildFile("Presets").getChildFile("User");
}

bool PresetManager::validatePresetFile(const juce::File& file)
{
    if (!file.existsAsFile()) return false;
    auto json = juce::JSON::parse(file);
    if (!json.isObject()) return false;
    if (!json.hasProperty("presetVersion")) return false;
    if (!json.hasProperty("presetName")) return false;
    return true;
}

juce::String PresetManager::computeChecksum(const juce::String& jsonContent)
{
    return juce::SHA256(jsonContent.toUTF8()).toHexString();
}
