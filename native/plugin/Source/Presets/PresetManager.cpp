#include "PresetManager.h"
#include "PresetSchema.h"
#include "FactoryPresets.h"
#include "HybridPresetV2.h"
#include "PresetMigration.h"
#include "HybridPresetApplier.h"
#include "../DSP/SampleLibrary.h"

// JUCE made AudioParameterChoice::setValue() private to discourage direct
// writes; it's still accessible through the AudioProcessorParameter base.
static inline void setParamRaw(juce::AudioProcessorParameter* p, float normalised)
{
    if (p != nullptr) p->setValue(normalised);
}

static void didaPresetManagerLog(const juce::String& message)
{
    juce::Logger::writeToLog(juce::String("[DIDITAGAIN preset-manager] ") + message);
}

PresetManager::PresetManager(juce::AudioProcessor& proc) : processor(proc)
{
    // Ensure factory directory contains the embedded presets the very first
    // time the plugin runs on this machine.
    auto factoryDir = getFactoryPresetDirectory();
    if (! factoryDir.exists()) factoryDir.createDirectory();
    dida::factory::extractMissing(factoryDir);

    // Make sure the user-facing samples folder exists so it's easy to find.
    auto samplesRoot = dida::SampleLibrary::getSamplesRoot();
    if (! samplesRoot.exists()) samplesRoot.createDirectory();

    // Pre-create one folder per broad category under Presets/User so the user
    // can just drop one-shots into the right bucket from the OS file browser.
    auto userPresetDir = getUserPresetDirectory();
    userPresetDir.createDirectory();
    for (auto& cat : dida::preset::dropCategories())
        userPresetDir.getChildFile(cat).createDirectory();

    scanPresetDirectory();
}

void PresetManager::scanPresetDirectory()
{
    presets.clear();
    loadFactoryPresets();
    loadUserPresets();
    loadDroppedSamples();
}

void PresetManager::loadDroppedSamples()
{
    auto root = getUserPresetDirectory();
    if (! root.isDirectory()) return;

    const juce::String wildcards = "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg";

    for (auto& cat : dida::preset::dropCategories())
    {
        auto dir = root.getChildFile(cat);
        if (! dir.isDirectory()) continue;

        // Non-recursive so users don't accidentally pick up backup folders.
        auto files = dir.findChildFiles(juce::File::findFiles, false, wildcards);
        // Stable, natural sort so renamed/added files keep predictable numbering.
        std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
            return a.getFileName().compareNatural(b.getFileName()) < 0;
        });

        int n = 1;
        for (auto& f : files)
        {
            // Skip backup/version artefacts created by the crop panel.
            const auto stem = f.getFileNameWithoutExtension();
            if (stem.endsWithIgnoreCase(".original")) continue;

            PresetInfo info;
            // Auto-numbered display name: "Guitar 1", "Pad 2", ...
            // Strip a trailing 's' so "Pianos" -> "Piano N", "Strings" -> "String N".
            juce::String singular = cat;
            if (singular.endsWithIgnoreCase("s") && singular.length() > 2)
                singular = singular.dropLastCharacters(1);
            info.name = singular + " " + juce::String(n++);
            info.author = "User";
            info.category = cat;
            info.description = f.getFileName();
            info.filePath = f.getFullPathName();        // doubles as identity
            info.isFactory = false;
            info.isSampleDrop = true;
            info.sampleSourcePath = f.getFullPathName();
            info.sampleRootMidi = 60;
            // Looping makes sense for sustained categories; not for transients.
            info.sampleLooping = (cat == "Pads" || cat == "Strings"
                                  || cat == "Choirs" || cat == "Brass"
                                  || cat == "Winds"  || cat == "Synths");
            presets.push_back(info);
        }
    }
}

static void readPresetInfoFromJson(const juce::var& json,
                                   const juce::File& file,
                                   bool isFactory,
                                   PresetInfo& info)
{
    // Support both v1 ("presetName") and v2 ("name") top-level keys.
    info.name        = json.hasProperty("name")
                         ? json.getProperty("name", "Untitled").toString()
                         : json.getProperty(dida::preset::key::presetName, "Untitled").toString();
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
//
// IMPORTANT: during preset load we deliberately use setValue() instead of
// setValueNotifyingHost(). Notifying the host on every parameter would flood
// FL Studio (and other DAWs) with what looks like a wall of automation
// events, which keeps the host's transport "active" and prevents pause /
// produces audible stutter while switching presets. APVTS still picks up the
// new values through its attached parameter listeners, so the engine sees
// the change on the next processBlock — we just don't pester the host.
static void setParam(juce::AudioProcessor& proc, const char* id, const juce::var& v)
{
    for (auto* param : proc.getParameters())
    {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
        {
            if (withId->paramID == id)
            {
                if (v.isBool())
                {
                    withId->setValue(v ? 1.0f : 0.0f);
                }
                else if (v.isString())
                {
                    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(withId))
                    {
                        const int idx = choice->choices.indexOf(v.toString(), true);
                        if (idx >= 0)
                            setParamRaw(choice, choice->convertTo0to1(static_cast<float>(idx)));
                    }
                }
                else if (v.isDouble() || v.isInt())
                {
                    const float fv = static_cast<float>((double) v);
                    if (auto* fp = dynamic_cast<juce::RangedAudioParameter*>(withId))
                        setParamRaw(fp, fp->convertTo0to1(fv));
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
                    setParamRaw(c, c->convertTo0to1(static_cast<float>(wf)));
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
        juce::String logMessage;
        logMessage << "ignored invalid index=" << index << " count=" << static_cast<int>(presets.size());
        didaPresetManagerLog(logMessage);
        return;
    }

    currentIndex = index;
    juce::String logMessage;
    logMessage << "load index=" << index << " name=" << presets[index].name << " file=" << presets[index].filePath;
    didaPresetManagerLog(logMessage);
    juce::File file(presets[index].filePath);
    loadPresetFromFile(file);
}

void PresetManager::loadPresetFromFile(const juce::File& file)
{
    if (!validatePresetFile(file))
    {
        didaPresetManagerLog(juce::String("validation failed file=") + file.getFullPathName());
        return;
    }

    auto json = juce::JSON::parse(file);
    if (! json.isObject())
    {
        didaPresetManagerLog(juce::String("parse failed file=") + file.getFullPathName());
        return;
    }

    using namespace dida::preset;

    // v2 hybrid presets: detect schemaVersion and translate Layer 1 + global
    // FX into the existing parameter set so they play even before the full
    // layered renderer is wired into Voice. Legacy v1 presets continue below.
    if (json.getProperty("schemaVersion", juce::var()).toString() == kSchemaVersionV2)
    {
        HybridPresetV2 p;
        if (PresetMigration::parseAny(json, p))
        {
            requestedInstrument = {};
            requestedSampleSource = {};
            requestedSampleDisplayName = {};
            requestedSampleRootMidi = 60;
            requestedSampleLooping = false;
            requestedCategory = p.category;

            auto applied = HybridPresetApplier::apply(p, processor);
            if (applied.hasSample)
            {
                requestedSampleSource      = applied.sampleSource;
                requestedSampleRootMidi    = applied.sampleRootMidi;
                requestedSampleDisplayName = applied.displayName;
                requestedSampleLooping     = applied.shouldLoop;
            }
            macroMapper.buildFrom(p, processor);

            if (onPresetLoaded) onPresetLoaded();
            juce::String logMessage;
            logMessage << "loaded v2 preset name=" << p.name
                << " category=" << p.category
                << " layers=" << (int) p.layers.size()
                << " loop=" << (requestedSampleLooping ? "true" : "false");
            didaPresetManagerLog(logMessage);
            return;
        }
    }
    // V1 path resets V2-only state too.
    requestedSampleLooping = false;
    requestedCategory = {};
    macroMapper.clear();

    // Sampler instrument (legacy)
    requestedInstrument = {};
    requestedSampleSource = {};
    requestedSampleDisplayName = {};
    requestedSampleRootMidi = 60;
    auto sampler = json.getProperty(key::sampler, juce::var());
    if (sampler.isObject())
        requestedInstrument = sampler.getProperty(key::instrument, "").toString();

    // Engine
    if (json.hasProperty(key::engineMode))
    {
        const int idx = engineModeFromString(json.getProperty(key::engineMode, "Subtractive").toString());
        for (auto* p : processor.getParameters())
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p))
                if (c->paramID == "engineMode")
                    setParamRaw(c, c->convertTo0to1(static_cast<float>(idx)));
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
                        setParamRaw(c, c->convertTo0to1(static_cast<float>(t)));
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

    juce::String logMessage;
    logMessage << "loaded file=" << file.getFileName()
        << " playMode=" << json.getProperty(key::playMode, "<missing>").toString()
        << " mono=" << json.getProperty(key::mono, "<missing>").toString()
        << " polyphony=" << json.getProperty(key::polyphony, "<missing>").toString();
    didaPresetManagerLog(logMessage);
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

int PresetManager::findPresetIndexByFile(const juce::File& file) const
{
    const auto target = file.getFullPathName();
    for (int i = 0; i < static_cast<int>(presets.size()); ++i)
        if (juce::File(presets[i].filePath).getFullPathName() == target)
            return i;

    return -1;
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
    // Must match where native/tools/import_samples.py writes presets:
    //   <UserDocuments>/DIDITAGAIN STUDIO/Presets/User/<Category>/*.didasynthpreset
    // Previously this pointed at AppData, so imported presets were invisible
    // to the in-plugin browser even after Rescan.
    return dida::SampleLibrary::getSamplesRoot()
        .getChildFile("Presets").getChildFile("User");
}

bool PresetManager::validatePresetFile(const juce::File& file)
{
    if (!file.existsAsFile()) return false;
    auto json = juce::JSON::parse(file);
    if (!json.isObject()) return false;
    // v2 uses schemaVersion+name; v1 used presetVersion+presetName.
    const bool v2 = json.hasProperty("schemaVersion") && json.hasProperty("name");
    const bool v1 = json.hasProperty(dida::preset::key::presetVersion)
                 && json.hasProperty(dida::preset::key::presetName);
    return v1 || v2;
}

juce::String PresetManager::computeChecksum(const juce::String& jsonContent)
{
    return juce::String::toHexString((juce::int64) jsonContent.hashCode64());
}
