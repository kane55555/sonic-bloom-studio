#include "PresetManager.h"
#include "PresetSchema.h"
#include "FactoryPresets.h"
#include "HybridPresetV2.h"
#include "PresetMigration.h"
#include "HybridPresetApplier.h"
#include "UserPresetLoader.h"
#include "UserPresetFormat.h"
#include "GuitarPresetBank.h"
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

    // Pre-create one folder per broad category under Samples/Presets/User so the
    // user can drop one-shots into the right bucket from the OS file browser.
    auto userPresetDir = getUserPresetDirectory();
    userPresetDir.createDirectory();
    for (auto& cat : dida::preset::dropCategories())
        userPresetDir.getChildFile(cat).createDirectory();

    seedGuitarPresetBankIfMissing();
    scanPresetDirectory();
}

void PresetManager::scanPresetDirectory()
{
    presets.clear();
    loadFactoryPresets();
    loadUserPresets();
    loadDiapresetFiles();
    loadDroppedSamples();
}

// Parse a trailing note token like "C3", "F#4", "Gb2", "Fs4" from a filename
// stem. Returns -1 if no recognisable note suffix is found.
static int parseRootMidiFromStem(const juce::String& stem)
{
    // Find last underscore/space/dash separator; the token after it is the note.
    int sep = -1;
    for (int i = stem.length() - 1; i >= 0; --i)
    {
        const auto c = stem[i];
        if (c == '_' || c == ' ' || c == '-') { sep = i; break; }
    }
    const juce::String tok = (sep >= 0 ? stem.substring(sep + 1) : stem).toUpperCase();
    if (tok.isEmpty()) return -1;

    const juce::juce_wchar letter = tok[0];
    if (letter < 'A' || letter > 'G') return -1;
    static const int semitone[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A..G
    int pc = semitone[letter - 'A'];

    int idx = 1;
    if (idx < tok.length())
    {
        const auto c = tok[idx];
        if (c == '#' || c == 'S') { pc += 1; ++idx; }
        else if (c == 'B' || c == 'F') {
            // "B"/"FLAT" accidental — but be careful not to swallow octave digits.
            if (c == 'B' && idx + 1 < tok.length() && juce::CharacterFunctions::isDigit(tok[idx + 1])) {
                pc -= 1; ++idx;
            } else if (tok.substring(idx).startsWithIgnoreCase("FLAT")) {
                pc -= 1; idx += 4;
            }
        }
    }
    const auto octStr = tok.substring(idx);
    if (octStr.isEmpty() || ! juce::CharacterFunctions::isDigit(octStr[0])) return -1;
    const int octave = octStr.getIntValue();
    const int midi = (octave + 1) * 12 + ((pc + 12) % 12);
    return (midi >= 0 && midi <= 127) ? midi : -1;
}

// Scan one "category root" directory. Each immediate subfolder is treated as
// a preset (multisample group of every WAV inside). Loose WAVs sitting
// directly in the category folder are collapsed into a single backward-compat
// preset named after the category itself.
static void scanCategoryFolder(const juce::File& categoryDir,
                               const juce::String& categoryName,
                               std::vector<PresetInfo>& outPresets)
{
    if (! categoryDir.isDirectory()) return;
    const juce::String wildcards = "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg";

    struct Group { juce::String name; juce::Array<juce::File> files; bool isFolder; juce::String folderPath; };
    juce::Array<Group> groups;
    auto findGroup = [&](const juce::String& key) -> Group* {
        for (auto& g : groups) if (g.name.equalsIgnoreCase(key)) return &g;
        return nullptr;
    };

    // 1) Subfolders → one preset per subfolder (multisample).
    auto subdirs = categoryDir.findChildFiles(juce::File::findDirectories, false);
    std::sort(subdirs.begin(), subdirs.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural(b.getFileName()) < 0;
    });
    for (auto& sub : subdirs)
    {
        auto subFiles = sub.findChildFiles(juce::File::findFiles, true, wildcards);
        if (subFiles.isEmpty()) continue;
        std::sort(subFiles.begin(), subFiles.end(), [](const juce::File& a, const juce::File& b) {
            return a.getFileName().compareNatural(b.getFileName()) < 0;
        });
        juce::Array<juce::File> filtered;
        for (auto& f : subFiles)
            if (! f.getFileNameWithoutExtension().endsWithIgnoreCase(".original"))
                filtered.add(f);
        if (filtered.isEmpty()) continue;
        groups.add({ sub.getFileName(), filtered, true, sub.getFullPathName() });
    }

    // 2) Loose files directly under category → single back-compat preset.
    auto looseFiles = categoryDir.findChildFiles(juce::File::findFiles, false, wildcards);
    std::sort(looseFiles.begin(), looseFiles.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural(b.getFileName()) < 0;
    });
    juce::Array<juce::File> looseFiltered;
    for (auto& f : looseFiles)
        if (! f.getFileNameWithoutExtension().endsWithIgnoreCase(".original"))
            looseFiltered.add(f);
    if (! looseFiltered.isEmpty())
    {
        // Avoid colliding with a subfolder of the same name.
        juce::String autoName = categoryName;
        if (findGroup(autoName) != nullptr) autoName += " (loose)";
        groups.add({ autoName, looseFiltered, false, {} });
    }

    const juce::String cat = categoryName;
    const bool sustained = (cat == "Pads" || cat == "Strings"
                            || cat == "Choirs" || cat == "Brass"
                            || cat == "Winds"  || cat == "Synths");

    for (auto& g : groups)
    {
        // Pick a representative sample: prefer the one closest to middle C
        // when notes are parseable; otherwise fall back to the first file.
        juce::File chosen = g.files.getFirst();
        int chosenRoot = 60;
        int bestDist = 9999;
        int parseable = 0;
        for (auto& f : g.files)
        {
            const int m = parseRootMidiFromStem(f.getFileNameWithoutExtension());
            if (m >= 0)
            {
                ++parseable;
                const int d = std::abs(m - 60);
                if (d < bestDist) { bestDist = d; chosen = f; chosenRoot = m; }
            }
        }

        if (g.files.isEmpty())
        {
            DBG("Preset folder has no WAV files: " << g.name);
            continue;
        }
        if (parseable == 0 && g.files.size() > 1)
            DBG("Preset folder has no parseable root notes: " << g.name);

        // Duplicate root-note warning.
        {
            juce::SortedSet<int> seen;
            for (auto& f : g.files)
            {
                const int m = parseRootMidiFromStem(f.getFileNameWithoutExtension());
                if (m < 0) continue;
                if (seen.contains(m))
                    DBG("Duplicate root note " << m << " in preset " << g.name);
                else
                    seen.add(m);
            }
        }

        PresetInfo info;
        info.name = g.name;
        info.author = "User";
        info.category = cat;
        info.description = juce::String(g.files.size()) + " samples";
        info.filePath = chosen.getFullPathName();
        info.isFactory = false;
        info.isSampleDrop = true;
        info.sampleSourcePath = chosen.getFullPathName();
        info.sampleRootMidi = chosenRoot;
        info.sampleLooping = sustained;
        info.sampleFolderPath = g.isFolder ? g.folderPath : juce::String();
        if (g.files.size() > 1)
        {
            for (auto& f : g.files)
                if (parseRootMidiFromStem(f.getFileNameWithoutExtension()) >= 0)
                    info.sampleSourcePaths.add(f.getFullPathName());
            if (info.sampleSourcePaths.size() < 2)
                info.sampleSourcePaths.clear();
        }

        // De-dupe: skip if another preset with the same category+name+path exists.
        bool dup = false;
        for (auto& existing : outPresets)
        {
            if (existing.category == info.category
                && existing.name.equalsIgnoreCase(info.name)
                && existing.filePath == info.filePath)
            { dup = true; break; }
        }
        if (! dup) outPresets.push_back(info);
    }
}

void PresetManager::loadDroppedSamples()
{
    // New layout: <Samples>/<Category>/<PresetName>/*.wav
    // Any immediate subfolder of the Samples root (other than "Presets") is
    // treated as a category. This matches what the user sees in the OS file
    // browser and avoids exposing individual WAV files as presets.
    auto samplesRoot = dida::SampleLibrary::getSamplesRoot();
    if (samplesRoot.isDirectory())
    {
        auto catDirs = samplesRoot.findChildFiles(juce::File::findDirectories, false);
        std::sort(catDirs.begin(), catDirs.end(), [](const juce::File& a, const juce::File& b) {
            return a.getFileName().compareNatural(b.getFileName()) < 0;
        });
        for (auto& d : catDirs)
        {
            const auto name = d.getFileName();
            if (name.equalsIgnoreCase("Presets")) continue; // reserved for .didasynthpreset trees
            scanCategoryFolder(d, name, presets);
        }
    }

    // Backwards compat: also scan the legacy drop tree at
    // <Samples>/Presets/User/<Category>/.
    auto legacyRoot = getUserPresetDirectory();
    if (legacyRoot.isDirectory())
    {
        for (auto& cat : dida::preset::dropCategories())
        {
            auto dir = legacyRoot.getChildFile(cat);
            if (dir.isDirectory())
                scanCategoryFolder(dir, cat, presets);
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
    const auto& info = presets[index];
    juce::String logMessage;
    logMessage << "load index=" << index << " name=" << info.name << " file=" << info.filePath;
    didaPresetManagerLog(logMessage);

    // ".diapreset" JSON preset → apply param snapshot + route multisample folder.
    if (info.isUserPreset)
    {
        dida::userpreset::UserPreset up;
        juce::String err;
        juce::File file(info.userPresetFile);
        if (! dida::userpreset::parseFile(file, up, err))
        {
            didaPresetManagerLog("diapreset parse failed file=" + file.getFullPathName() + " err=" + err);
            return;
        }

        auto resolved = dida::userpreset::resolveSourcePath(up.source.path);
        if (! resolved.isDirectory())
            didaPresetManagerLog("diapreset source folder missing path=" + up.source.path);

        // Route the multisample folder via the existing engine path.
        requestedInstrument        = {};
        requestedSampleSource      = {};
        requestedSampleSources.clear();
        requestedSampleFolderPath  = resolved.getFullPathName();
        requestedSampleDisplayName = up.presetName;
        requestedSampleRootMidi    = 60;
        requestedSampleLooping     = false;
        requestedCategory          = up.category;
        macroMapper.clear();

        dida::userpreset::applyToProcessor(up, processor);

        didaPresetManagerLog("loaded diapreset name=" + up.presetName
            + " category=" + up.category
            + " folder=" + requestedSampleFolderPath);

        if (onPresetLoaded) onPresetLoaded();
        return;
    }

    if (info.isSampleDrop)
    {
        // No JSON to parse — just route the dropped one-shot into the engine.
        requestedInstrument        = {};
        requestedSampleSource      = info.sampleSourcePath;
        requestedSampleSources     = info.sampleSourcePaths;
        requestedSampleFolderPath  = info.sampleFolderPath;
        requestedSampleDisplayName = info.name;
        requestedSampleRootMidi    = info.sampleRootMidi;
        requestedSampleLooping     = info.sampleLooping;
        requestedCategory          = info.category;
        macroMapper.clear();

        // Reset the synth voice so the dropped sample is what you hear — not
        // whatever oscillator/sub/noise levels the previous preset left armed.
        setParam(processor, "oscALevel",       1.0);
        setParam(processor, "oscBLevel",       0.0);
        setParam(processor, "subOscLevel",     0.0);
        setParam(processor, "subOscEnabled",   false);
        setParam(processor, "noiseLevel",      0.0);
        setParam(processor, "filter1Cutoff",   20000.0);
        setParam(processor, "filter1Resonance",0.0);
        setParam(processor, "filter1Drive",    0.0);
        setParam(processor, "env1Attack",      0.005);
        setParam(processor, "env1Decay",       0.1);
        setParam(processor, "env1Sustain",     1.0);
        setParam(processor, "env1Release",     0.2);

        if (onPresetLoaded) onPresetLoaded();
        return;
    }

    juce::File file(info.filePath);
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
            requestedSampleSources.clear();
            requestedSampleFolderPath = {};
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
    requestedSampleSources.clear();
    requestedSampleFolderPath = {};
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
    // User-facing drop folders:
    //   <UserDocuments>/DIDITAGAIN STUDIO/Samples/Presets/User/<Category>/
    // Raw one-shots dropped here are treated as sample-backed user presets.
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
