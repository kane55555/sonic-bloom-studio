#include "PresetManager.h"
#include "PresetSchema.h"
#include "FactoryPresets.h"

#define DIDA_PRESET_MANAGER_LOG(message) \
    juce::Logger::writeToLog(juce::String("[DIDITAGAIN preset-manager] ") + (juce::String() << message))

PresetManager::PresetManager(juce::AudioProcessor& proc) : processor(proc)
{
    // Ensure factory directory contains the embedded presets the very first
    // time the plugin runs on this machine.
    auto factoryDir = getFactoryPresetDirectory();
    if (! factoryDir.exists()) factoryDir.createDirectory();
    dida::factory::extractMissing(factoryDir);

    scanPresetDirectory();
}

void PresetManager::scanPresetDirectory()
{
    presets.clear();
    loadFactoryPresets();
    loadUserPresets();
}

static void readPresetInfoFromJson(const juce::var& json,
                                   const juce::File& file,
                                   bool isFactory,
                                   PresetInfo& info)
{
    info.name        = json.getProperty(dida::preset::key::presetName, "Untitled").toString();
    info.author      = json.getProperty(dida::preset::key::author, "DIDITAGAIN").toString();
    info.category    = json.getProperty(dida::preset::key::category, "Init").toString();
    info.description = json.getProperty(dida::preset::key::description, "").toString();
    info.filePath    = file.getFullPathName();
    info.isFactory   = isFactory;

    auto tagsVar = json.getProperty(dida::preset::key::tags, juce::var());
    if (auto* tagsArray = tagsVar.getArray())
        for (auto& t : *tagsArray)
            info.tags.add(t.toString());
}

void PresetManager::loadFactoryPresets()
{
    auto dir = getFactoryPresetDirectory();
    if (!dir.isDirectory()) return;

    auto files = dir.findChildFiles(juce::File::findFiles, true, "*.didasynthpreset");
    for (auto& file : files)
    {
        auto json = juce::JSON::parse(file);
        if (! json.isObject()) continue;
        PresetInfo info;
        readPresetInfoFromJson(json, file, /*isFactory*/true, info);
        presets.push_back(info);
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
        if (! json.isObject()) continue;
        PresetInfo info;
        readPresetInfoFromJson(json, file, /*isFactory*/false, info);
        presets.push_back(info);
    }
}

// Helper: set an APVTS parameter from a juce::var, normalising the value.
static void setParam(juce::AudioProcessor& proc, const char* id, const juce::var& v)
{
    // Walk parameters by ID via the host-style API.
    for (auto* param : proc.getParameters())
    {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
        {
            if (withId->paramID == id)
            {
                if (v.isBool())
                {
                    withId->setValueNotifyingHost(v ? 1.0f : 0.0f);
                }
                else if (v.isString())
                {
                    // Choice parameter — try to map by text.
                    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(withId))
                    {
                        const int idx = choice->choices.indexOf(v.toString(), true);
                        if (idx >= 0)
                            choice->setValueNotifyingHost(choice->convertTo0to1(static_cast<float>(idx)));
                    }
                }
                else if (v.isDouble() || v.isInt())
                {
                    const float fv = static_cast<float>((double) v);
                    if (auto* fp = dynamic_cast<juce::RangedAudioParameter*>(withId))
                        fp->setValueNotifyingHost(fp->convertTo0to1(fv));
                }
                return;
            }
        }
    }
}

static void applyOscBlock(juce::AudioProcessor& proc, const juce::var& obj,
                          const char* wfId, const char* lvlId, const char* detId,
                          const char* octId, const char* semiId, const char* pwId = nullptr)
{
    if (! obj.isObject()) return;
    if (obj.hasProperty("waveform"))
    {
        const int wf = dida::preset::waveformFromString(obj.getProperty("waveform","").toString());
        // choice parameters use index; convert to normalised
        for (auto* p : proc.getParameters())
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p))
                if (c->paramID == wfId)
                    c->setValueNotifyingHost(c->convertTo0to1(static_cast<float>(wf)));
    }
    if (obj.hasProperty("level"))       setParam(proc, lvlId,  obj.getProperty("level", 0.0));
    if (obj.hasProperty("detuneCents")) setParam(proc, detId,  obj.getProperty("detuneCents", 0.0));
    if (obj.hasProperty("octave"))      setParam(proc, octId,  obj.getProperty("octave", 0));
    if (obj.hasProperty("semitone"))    setParam(proc, semiId, obj.getProperty("semitone", 0));
    if (pwId && obj.hasProperty("pulseWidth")) setParam(proc, pwId, obj.getProperty("pulseWidth", 0.5));
}

static void applyEnv(juce::AudioProcessor& proc, const juce::var& obj, const char* prefix)
{
    if (! obj.isObject()) return;
    juce::String pfx(prefix);
    setParam(proc, (pfx + "Attack").toRawUTF8(),  obj.getProperty("attack",  0.01));
    setParam(proc, (pfx + "Decay").toRawUTF8(),   obj.getProperty("decay",   0.3));
    setParam(proc, (pfx + "Sustain").toRawUTF8(), obj.getProperty("sustain", 0.7));
    setParam(proc, (pfx + "Release").toRawUTF8(), obj.getProperty("release", 0.5));
}

void PresetManager::loadPreset(int index)
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
    {
        DIDA_PRESET_MANAGER_LOG("ignored invalid index=" << index << " count=" << static_cast<int>(presets.size()));
        return;
    }

    currentIndex = index;
    DIDA_PRESET_MANAGER_LOG("load index=" << index << " name=" << presets[index].name << " file=" << presets[index].filePath);
    juce::File file(presets[index].filePath);
    loadPresetFromFile(file);
}

void PresetManager::loadPresetFromFile(const juce::File& file)
{
    if (!validatePresetFile(file))
    {
        DIDA_PRESET_MANAGER_LOG("validation failed file=" << file.getFullPathName());
        return;
    }

    auto json = juce::JSON::parse(file);
    if (! json.isObject())
    {
        DIDA_PRESET_MANAGER_LOG("parse failed file=" << file.getFullPathName());
        return;
    }

    using namespace dida::preset;

    // Engine
    if (json.hasProperty(key::engineMode))
    {
        const int idx = engineModeFromString(json.getProperty(key::engineMode, "Subtractive").toString());
        for (auto* p : processor.getParameters())
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p))
                if (c->paramID == "engineMode")
                    c->setValueNotifyingHost(c->convertTo0to1(static_cast<float>(idx)));
    }
    if (json.hasProperty(key::masterGain)) setParam(processor, "masterGain", json.getProperty(key::masterGain, 0.0));
    if (json.hasProperty(key::polyphony))  setParam(processor, "polyphony",  json.getProperty(key::polyphony, 8));
    if (json.hasProperty(key::mono))       setParam(processor, "monoMode",   json.getProperty(key::mono, false));
    if (json.hasProperty(key::playMode))
    {
        const bool isMono = json.getProperty(key::playMode, "poly").toString().equalsIgnoreCase("mono");
        setParam(processor, "monoMode", isMono);
    }
    if (json.hasProperty(key::glideMs))
    {
        const double ms = (double) json.getProperty(key::glideMs, 0.0);
        setParam(processor, "glideTime", ms / 1000.0);
    }
    if (json.hasProperty(key::glideTime))
        setParam(processor, "glideTime", (double) json.getProperty(key::glideTime, 0.0));

    // Oscillators
    applyOscBlock(processor, json.getProperty(key::oscA, juce::var()),
                  "oscAWaveform","oscALevel","oscADetune","oscAOctave","oscASemi","oscAPulseWidth");
    applyOscBlock(processor, json.getProperty(key::oscB, juce::var()),
                  "oscBWaveform","oscBLevel","oscBDetune","oscBOctave","oscBSemi");

    auto sub = json.getProperty(key::subOsc, juce::var());
    if (sub.isObject())
    {
        if (sub.hasProperty("enabled")) setParam(processor, "subOscEnabled", sub.getProperty("enabled", false));
        if (sub.hasProperty("level"))   setParam(processor, "subOscLevel",   sub.getProperty("level", 0.0));
    }
    auto noise = json.getProperty(key::noise, juce::var());
    if (noise.isObject() && noise.hasProperty("level"))
        setParam(processor, "noiseLevel", noise.getProperty("level", 0.0));

    // Filter 1
    auto f1 = json.getProperty(key::filter1, juce::var());
    if (f1.isObject())
    {
        if (f1.hasProperty("type"))
        {
            const int t = filterTypeFromString(f1.getProperty("type","LP24").toString());
            for (auto* p : processor.getParameters())
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p))
                    if (c->paramID == "filter1Type")
                        c->setValueNotifyingHost(c->convertTo0to1(static_cast<float>(t)));
        }
        setParam(processor, "filter1Cutoff",     f1.getProperty("cutoff",     8000.0));
        setParam(processor, "filter1Resonance",  f1.getProperty("resonance",  0.2));
        setParam(processor, "filter1Drive",      f1.getProperty("drive",      0.0));
        setParam(processor, "filter1EnvAmount",  f1.getProperty("envAmount",  0.0));
        setParam(processor, "filter1KeyTrack",   f1.getProperty("keyTrack",   0.0));
    }

    applyEnv(processor, json.getProperty(key::env1, juce::var()), "env1");
    applyEnv(processor, json.getProperty(key::env2, juce::var()), "env2");
    applyEnv(processor, json.getProperty(key::env3, juce::var()), "env3");

    // FX chain
    auto fx = json.getProperty(key::fxChain, juce::var());
    if (fx.isObject())
    {
        setParam(processor, "fxChorusMix",        fx.getProperty("chorusMix", 0.0));
        setParam(processor, "fxDelayMix",         fx.getProperty("delayMix",  0.0));
        setParam(processor, "fxDelayTime",        fx.getProperty("delayTime", 0.3));
        setParam(processor, "fxDelayFeedback",    fx.getProperty("delayFeedback", 0.4));
        setParam(processor, "fxReverbMix",        fx.getProperty("reverbMix", 0.0));
        setParam(processor, "fxReverbSize",       fx.getProperty("reverbSize", 0.5));
        setParam(processor, "fxDistortionAmount", fx.getProperty("distortionAmount", 0.0));
    }

    if (onPresetLoaded)
        onPresetLoaded();

    DIDA_PRESET_MANAGER_LOG("loaded file=" << file.getFileName()
        << " playMode=" << json.getProperty(key::playMode, "<missing>").toString()
        << " mono=" << json.getProperty(key::mono, "<missing>").toString()
        << " polyphony=" << json.getProperty(key::polyphony, "<missing>").toString());
}

void PresetManager::saveCurrentPreset(const juce::String& name, const juce::String& category)
{
    auto dir = getUserPresetDirectory();
    dir.createDirectory();
    auto file = dir.getChildFile(name + ".didasynthpreset");

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty(dida::preset::key::presetVersion, dida::preset::kSchemaVersion);
    obj->setProperty(dida::preset::key::presetName, name);
    obj->setProperty(dida::preset::key::author, "User");
    obj->setProperty(dida::preset::key::category, category);
    obj->setProperty(dida::preset::key::tags, juce::var());

    // Snapshot every parameter.
    juce::DynamicObject::Ptr params = new juce::DynamicObject();
    for (auto* p : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            params->setProperty(withId->paramID, withId->getValue());
    obj->setProperty("parameters", juce::var(params.get()));

    auto jsonStr = juce::JSON::toString(juce::var(obj.get()));
    obj->setProperty(dida::preset::key::checksum, computeChecksum(jsonStr));

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
    return juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory)
        .getChildFile("DIDITAGAIN").getChildFile("DIDITAGAIN STUDIO")
        .getChildFile("Presets").getChildFile("Factory");
}

juce::File PresetManager::getUserPresetDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DIDITAGAIN").getChildFile("DIDITAGAIN STUDIO")
        .getChildFile("Presets").getChildFile("User");
}

bool PresetManager::validatePresetFile(const juce::File& file)
{
    if (!file.existsAsFile()) return false;
    auto json = juce::JSON::parse(file);
    if (!json.isObject()) return false;
    if (!json.hasProperty(dida::preset::key::presetVersion)) return false;
    if (!json.hasProperty(dida::preset::key::presetName))    return false;
    return true;
}

juce::String PresetManager::computeChecksum(const juce::String& jsonContent)
{
    return juce::String::toHexString((juce::int64) jsonContent.hashCode64());
}
