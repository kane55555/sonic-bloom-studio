#include "PresetManager.h"
#include "PresetSchema.h"
#include "FactoryPresets.h"
#include "HybridPresetV2.h"
#include "PresetMigration.h"
#include "HybridPresetApplier.h"
#include "PresetQualityReport.h"
#include "UserPresetLoader.h"
#include "UserPresetFormat.h"
#include "GuitarPresetBank.h"
#include "VintageSynthBank.h"
#include "../DSP/SampleLibrary.h"
#include "../PluginProcessor.h"
#include "../DSP/SynthEngine.h"
#include <limits>
#include <cmath>

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
    seedVintageSynthBankIfMissing();

    // Drop a one-time .seeded marker into every User category folder. Any
    // future auto-seeding logic for these folders must check this marker and
    // skip when present, so presets the user deletes never reappear.
    for (auto& cat : dida::preset::dropCategories())
    {
        auto marker = userPresetDir.getChildFile(cat).getChildFile(".seeded");
        if (! marker.existsAsFile())
            marker.replaceWithText("1");
    }

    scanPresetDirectory();
}

void PresetManager::scanPresetDirectory()
{
    presets.clear();
    loadFactoryPresets();
    loadUserPresets();
    loadDiapresetFiles();

    // Browser source of truth: only .diapreset files inside
    // <Samples>/Presets/User/<Category>/ should appear in the user-facing
    // browser. Raw sample folders under <Samples>/<Category>/<Folder>/ are
    // hidden base instruments and must NOT show as presets. The legacy
    // dropped-sample scan is preserved for developer/debug builds only.
    if (showSampleFoldersInBrowser)
        loadDroppedSamples();

    int diapresetCount = 0;
    juce::StringArray seenCats;
    for (auto& p : presets)
    {
        if (p.isUserPreset) ++diapresetCount;
        if (! seenCats.contains(p.category)) seenCats.add(p.category);
    }
    juce::Logger::writeToLog(juce::String("[DIDITAGAIN browser] visiblePresets=")
        + juce::String(diapresetCount)
        + " categories=" + juce::String(seenCats.size())
        + " sampleFoldersExposed=" + juce::String(showSampleFoldersInBrowser ? 1 : 0));
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

static bool isSustainedSampleCategory(const juce::String& category)
{
    return category == "Pads" || category == "Strings"
        || category == "Choirs" || category == "Brass"
        || category == "Winds"  || category == "Synths";
}

static juce::File chooseRepresentativeMappedWav(const juce::Array<juce::File>& files, int& rootMidiOut)
{
    juce::File chosen = files.getFirst();
    rootMidiOut = 60;
    int bestScore = std::numeric_limits<int>::max();

    for (auto& f : files)
    {
        const int midi = parseRootMidiFromStem(f.getFileNameWithoutExtension());
        if (midi < 0) continue;

        // Prefer C4 if present, otherwise C3, then the nearest mapped root.
        const int score = juce::jmin(std::abs(midi - 60), std::abs(midi - 48) + 1);
        if (score < bestScore)
        {
            bestScore = score;
            chosen = f;
            rootMidiOut = midi;
        }
    }

    return chosen;
}

// Normalise a category string: collapse common typos / camelCase aliases.
// Used for caps lookup, folder-grouping, debug reports and validation. Does
// NOT rename anything on disk — purely an in-memory canonicalisation.
static juce::String normalizeCategoryAlias(const juce::String& in)
{
    auto s = in.trim();
    if (s.isEmpty()) return s;
    s = s.replace("Saxaphone", "Saxophone", true);  // common typo
    s = s.replace("saxaphone", "saxophone", true);
    if (s.equalsIgnoreCase("VintageSynth") || s.equalsIgnoreCase("VintageSynths"))
        return "Vintage Synths";
    return s;
}

// Strip recognised qualifier prefixes ("Trap Saxophone" -> "Saxophone")
// so categories with stylistic qualifiers still map back to their base
// instrument for caps/blend-mode lookup.
static juce::String stripCategoryQualifier(juce::String s)
{
    for (auto* pfx : { "Trap ", "Lo-Fi ", "Lofi ", "Vintage " })
    {
        const juce::String p (pfx);
        if (s.startsWithIgnoreCase(p)) { s = s.substring(p.length()).trim(); break; }
    }
    return s;
}

static bool categoryNamesMatch(const juce::String& a, const juce::String& b)
{
    const auto aa = normalizeCategoryAlias(a).trim();
    const auto bb = normalizeCategoryAlias(b).trim();
    if (aa.equalsIgnoreCase(bb)) return true;
    if (aa.endsWithIgnoreCase("s") && aa.dropLastCharacters(1).equalsIgnoreCase(bb)) return true;
    if (bb.endsWithIgnoreCase("s") && bb.dropLastCharacters(1).equalsIgnoreCase(aa)) return true;
    const auto sa = stripCategoryQualifier(aa);
    const auto sb = stripCategoryQualifier(bb);
    if (sa.equalsIgnoreCase(sb)) return true;
    if (sa.endsWithIgnoreCase("s") && sa.dropLastCharacters(1).equalsIgnoreCase(sb)) return true;
    if (sb.endsWithIgnoreCase("s") && sb.dropLastCharacters(1).equalsIgnoreCase(sa)) return true;
    return false;
}

static bool folderHasMappedAudio(const juce::File& folder, bool recursive)
{
    if (! folder.isDirectory()) return false;
    const auto files = folder.findChildFiles(juce::File::findFiles, recursive,
                                             "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    for (auto& f : files)
        if (! f.getFileNameWithoutExtension().endsWithIgnoreCase(".original")
            && parseRootMidiFromStem(f.getFileNameWithoutExtension()) >= 0)
            return true;
    return false;
}

// Looser: any audio file at all (used as a fallback when a folder contains
// samples that aren't named with a midi-root suffix, e.g. "Alto Sax.wav").
static bool folderHasAnyAudio(const juce::File& folder, bool recursive)
{
    if (! folder.isDirectory()) return false;
    const auto files = folder.findChildFiles(juce::File::findFiles, recursive,
                                             "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    for (auto& f : files)
        if (! f.getFileNameWithoutExtension().endsWithIgnoreCase(".original"))
            return true;
    return false;
}

static bool pathLivesInCategory(const juce::File& folder, const juce::String& category)
{
    if (! folder.isDirectory() || category.trim().isEmpty()) return false;
    auto cursor = folder;
    for (int guard = 0; guard < 24 && cursor.getFullPathName().isNotEmpty(); ++guard)
    {
        if (categoryNamesMatch(cursor.getFileName(), category)) return true;
        const auto parent = cursor.getParentDirectory();
        if (parent == cursor) break;
        cursor = parent;
    }
    return false;
}

// Resolve the user's category folder tolerantly: exact name, then any
// immediate child of userPresetRoot that matches via categoryNamesMatch
// (covers plural/singular drift like "Saxophones" vs "Saxophone").
static juce::File resolveCategoryDir(const juce::File& userPresetRoot, const juce::String& category)
{
    auto exact = userPresetRoot.getChildFile(category);
    if (exact.isDirectory()) return exact;
    for (auto& sub : userPresetRoot.findChildFiles(juce::File::findDirectories, false))
        if (categoryNamesMatch(sub.getFileName(), category))
            return sub;
    return {};
}

static juce::File findCategorySourceFolder(const juce::File& userPresetRoot,
                                           const juce::String& category,
                                           const juce::String& preferredLeaf,
                                           const juce::File& presetFile)
{
    auto catDir = resolveCategoryDir(userPresetRoot, category);
    if (! catDir.isDirectory()) return {};

    // 1) Walk up from the preset file looking for the nearest folder with audio.
    for (auto cursor = presetFile.getParentDirectory();
         cursor.isDirectory() && cursor != catDir.getParentDirectory();)
    {
        if (cursor == catDir)
        {
            if (folderHasAnyAudio(cursor, false)) return cursor;
            break;
        }
        if (folderHasAnyAudio(cursor, true)) return cursor;

        const auto parent = cursor.getParentDirectory();
        if (parent == cursor) break;
        cursor = parent;
    }

    auto subdirs = catDir.findChildFiles(juce::File::findDirectories, true);
    std::sort(subdirs.begin(), subdirs.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFullPathName().compareNatural(b.getFullPathName()) < 0;
    });

    // 2) Honour the preset's named source folder if it exists in this category.
    if (preferredLeaf.isNotEmpty())
        for (auto& sub : subdirs)
            if (sub.getFileName().equalsIgnoreCase(preferredLeaf)
                && folderHasAnyAudio(sub, true))
                return sub;

    // 3) Prefer mapped audio (midi-root suffix) anywhere in the category.
    if (folderHasMappedAudio(catDir, false)) return catDir;
    for (auto& sub : subdirs)
        if (folderHasMappedAudio(sub, true))
            return sub;

    // 4) Final fallback — first subfolder with any audio (e.g. "Saxophone 1"),
    //    matching the user's rule: "always link to instrument 1 in the folder".
    if (folderHasAnyAudio(catDir, false)) return catDir;
    for (auto& sub : subdirs)
        if (folderHasAnyAudio(sub, true))
            return sub;

    return {};
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
    const bool sustained = isSustainedSampleCategory(cat);

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
        {
            DBG("Preset folder has no parseable root notes: " << g.name);
        }

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

    // Scan every immediate subfolder of <Samples>/Presets/User/ as its own
    // category, using the folder name exactly as typed by the user. This
    // covers both the built-in dropCategories() folders and any custom
    // folder the user creates (e.g. "My Trumpets", "Halloween FX").
    auto legacyRoot = getUserPresetDirectory();
    if (legacyRoot.isDirectory())
    {
        auto userDirs = legacyRoot.findChildFiles(juce::File::findDirectories, false);
        std::sort(userDirs.begin(), userDirs.end(), [](const juce::File& a, const juce::File& b) {
            return a.getFileName().compareNatural(b.getFileName()) < 0;
        });
        for (auto& dir : userDirs)
            scanCategoryFolder(dir, dir.getFileName(), presets);
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

static juce::String getParamDebugValue(juce::AudioProcessor& proc, const char* id)
{
    for (auto* param : proc.getParameters())
    {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
        {
            if (withId->paramID == id)
            {
                if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(withId))
                {
                    const int idx = choice->getIndex();
                    if (choice->choices.isEmpty())
                        return juce::String(idx);
                    const int clamped = juce::jlimit(0, choice->choices.size() - 1, idx);
                    return choice->choices[clamped] + "(" + juce::String(clamped) + ")";
                }
                auto* base = static_cast<juce::AudioProcessorParameter*>(param);
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(withId))
                    return juce::String(ranged->convertFrom0to1(base->getValue()), 4);

                return juce::String(base->getValue(), 4);
            }
        }
    }
    return "<missing>";
}

static void logFinalActivePresetParams(juce::AudioProcessor& proc, const juce::String& presetName)
{
    didaPresetManagerLog(juce::String("FINAL ACTIVE PRESET PARAMS")
        + " presetName=" + presetName
        + " filter1Type=" + getParamDebugValue(proc, "filter1Type")
        + " filter1Cutoff=" + getParamDebugValue(proc, "filter1Cutoff")
        + " env1Attack=" + getParamDebugValue(proc, "env1Attack")
        + " env1Release=" + getParamDebugValue(proc, "env1Release")
        + " fxChorusMix=" + getParamDebugValue(proc, "fxChorusMix")
        + " fxDelayMix=" + getParamDebugValue(proc, "fxDelayMix")
        + " fxReverbMix=" + getParamDebugValue(proc, "fxReverbMix")
        + " fxDistortionAmount=" + getParamDebugValue(proc, "fxDistortionAmount")
        + " oscBLevel=" + getParamDebugValue(proc, "oscBLevel"));
}

void PresetManager::applyPendingUserDiapresetAfterSampleLoad()
{
    if (! pendingUserDiapresetApply)
        return;

    pendingUserDiapresetApply = false;
    didaPresetManagerLog("applying diapreset sound design after source load: " + pendingUserDiapreset.presetName);
    dida::userpreset::applyToProcessor(pendingUserDiapreset, processor);
    if (auto* dp = dynamic_cast<DiditagainProcessor*>(&processor))
    {
        auto& pfx = dp->getSynthEngine().getFx();
        const auto c = pendingUserDiapreset.category.toLowerCase();
        if (c.contains("808") || c.contains("bass") || c.contains("sub"))
        { pfx.setReverbInputHighPassHz(360.0f); pfx.setReverbInputLowPassHz(4200.0f); pfx.setReverbDiffusion(0.45f); pfx.setReverbDucking(0.10f, 4.0f, 180.0f); pfx.setReverbLowMonoControl(340.0f, 0.0f); pfx.setReverbWidth(0.25f); }
        else if (c.contains("brass") || c.contains("trumpet") || c.contains("horn") || c.contains("trap"))
        { pfx.setReverbInputHighPassHz(240.0f); pfx.setReverbInputLowPassHz(5600.0f); pfx.setReverbDiffusion(0.54f); pfx.setReverbDucking(0.28f, 4.0f, 220.0f); pfx.setReverbLowMonoControl(320.0f, 0.03f); pfx.setReverbWidth(0.78f); }
        else if (c.contains("pad") || c.contains("choir") || c.contains("vox") || c.contains("vocal") || c.contains("string") || c.contains("texture"))
        { pfx.setReverbInputHighPassHz(350.0f); pfx.setReverbInputLowPassHz(7400.0f); pfx.setReverbDiffusion(0.69f); pfx.setReverbDucking(0.28f, 8.0f, 380.0f); pfx.setReverbLowMonoControl(350.0f, 0.04f); pfx.setReverbWidth(0.92f); }
        else if (c.contains("guitar"))
        { pfx.setReverbInputHighPassHz(280.0f); pfx.setReverbInputLowPassHz(4800.0f); pfx.setReverbDiffusion(0.58f); pfx.setReverbDucking(0.22f, 5.0f, 260.0f); pfx.setReverbLowMonoControl(300.0f, 0.05f); pfx.setReverbWidth(0.72f); }
        else if (c.contains("lead"))
        { pfx.setReverbInputHighPassHz(210.0f); pfx.setReverbInputLowPassHz(8500.0f); pfx.setReverbDiffusion(0.62f); pfx.setReverbDucking(0.23f, 5.0f, 240.0f); pfx.setReverbLowMonoControl(300.0f, 0.06f); pfx.setReverbWidth(0.86f); }
        else
        { pfx.setReverbCharacter(ReverbBlock::Character::Studio); }
    }
    logFinalActivePresetParams(processor, pendingUserDiapreset.presetName);

    // Debug: emit one structured preset-quality block per .diapreset load.
    if (auto* dp = dynamic_cast<DiditagainProcessor*>(&processor))
        dida::presetreport::report(*dp, pendingUserDiapreset, requestedCategory,
                                   requestedSampleFolderPath,
                                   requestedSampleSources.size());
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
    requestedPresetIsUserDiapreset = false;
    pendingUserDiapresetApply = false;

    // ".diapreset" JSON preset → route the multisample folder first; the
    // processor applies the sound-design params after the source is active.
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

        didaPresetManagerLog("loading diapreset: " + up.presetName);

        const auto effectiveCategoryRaw = info.category.isNotEmpty()
            ? info.category
            : (up.category.isNotEmpty() ? up.category : juce::String("User"));
        const auto effectiveCategory = normalizeCategoryAlias(effectiveCategoryRaw);
        const auto sourceLeaf = juce::File(up.source.path.replaceCharacter('\\', '/')).getFileName();

        const auto rawSourcePath = up.source.path.trim();
        const auto rawNormSlash  = rawSourcePath.replaceCharacter('\\', '/');
        const bool rawIsAbsolute = rawSourcePath.isNotEmpty()
                                && juce::File::isAbsolutePath(rawNormSlash);
        const bool rawIsInsidePresetsUser = rawNormSlash.containsIgnoreCase("/Samples/Presets/User/");

        // STEP 1 — Honour an absolute sourceInstrument.path exactly. The
        // .diapreset is the source of truth: do NOT rewrite Samples/Pianos/...
        // under Samples/Presets/User/.
        juce::File resolved;
        juce::String resolvedFrom;
        if (rawIsAbsolute)
        {
            auto abs = dida::userpreset::resolveSourcePath(rawSourcePath);
            if (abs.isDirectory())
            {
                resolved = abs;
                resolvedFrom = "absoluteSourceInstrumentPath";
                didaPresetManagerLog("diapreset using absolute source path=" + resolved.getFullPathName());
            }
        }

        // STEP 2 — Fallback only when no absolute resolution worked: search
        // the user category folder (legacy behaviour for old presets that
        // only stored a relative leaf name).
        if (! resolved.isDirectory())
        {
            auto catResolved = findCategorySourceFolder(getUserPresetDirectory(), effectiveCategory, sourceLeaf, file);
            if (catResolved.isDirectory())
            {
                resolved = catResolved;
                resolvedFrom = "fallbackSearch";
                didaPresetManagerLog("diapreset routed within folder category=" + effectiveCategory
                    + " folder=" + resolved.getFullPathName());
            }
        }

        // STEP 3 — Last-chance discovery via name/category search.
        if (! resolved.isDirectory() && rawSourcePath.isNotEmpty())
        {
            auto discovered = dida::userpreset::resolveSourcePath(rawSourcePath);
            if (discovered.isDirectory())
            {
                resolved = discovered;
                if (resolvedFrom.isEmpty()) resolvedFrom = "fallbackSearch";
            }
        }

        if (! resolved.isDirectory())
        {
            didaPresetManagerLog("diapreset source folder missing in category=" + effectiveCategory
                + " path=" + up.source.path);
        }

        // Common reset of sample state; we re-fill it below when we have a folder.
        requestedInstrument        = {};
        requestedSampleSource      = {};
        requestedSampleSources.clear();
        requestedSampleFolderPath  = {};
        requestedSampleDisplayName = up.presetName;
        requestedSampleRootMidi    = 60;
        requestedSampleLooping     = isSustainedSampleCategory(effectiveCategory);
        requestedCategory          = effectiveCategory;
        macroMapper.clear();
        requestedPresetIsUserDiapreset = true;
        pendingUserDiapreset = up;
        pendingUserDiapresetApply = true;

        if (resolved.isDirectory())
        {
            didaPresetManagerLog("resolved source folder: " + resolved.getFullPathName());

            auto files = resolved.findChildFiles(juce::File::findFiles, true, "*.wav");
            std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
                return a.getFileName().compareNatural(b.getFileName()) < 0;
            });

            for (auto& f : files)
                if (parseRootMidiFromStem(f.getFileNameWithoutExtension()) >= 0)
                    requestedSampleSources.add(f.getFullPathName());

            // Fallback: folder has audio but none follow the midi-root naming
            // convention (e.g. "Alto Sax.wav"). Still route it — assume root C4.
            if (requestedSampleSources.isEmpty())
                for (auto& f : files)
                    if (! f.getFileNameWithoutExtension().endsWithIgnoreCase(".original"))
                        requestedSampleSources.add(f.getFullPathName());

            didaPresetManagerLog("found WAV count: " + juce::String(files.size()));
            didaPresetManagerLog("valid mapped WAV count: " + juce::String(requestedSampleSources.size()));

            if (! requestedSampleSources.isEmpty())
            {
                juce::Array<juce::File> mappedFiles;
                for (auto& p : requestedSampleSources)
                    mappedFiles.add(juce::File(p));

                int representativeRoot = 60;
                const auto representative = chooseRepresentativeMappedWav(mappedFiles, representativeRoot);

                requestedSampleSource      = representative.getFullPathName();
                requestedSampleFolderPath  = resolved.getFullPathName();
                requestedSampleRootMidi    = representativeRoot;
            }
            else
            {
                didaPresetManagerLog("diapreset source folder has no mapped WAVs; falling back to factory synth name="
                    + up.presetName);
            }
        }
        else
        {
            // No instrument folder available (e.g. Synths category with no
            // dropped sample folder). The factory synth will play the preset's
            // sound-design directly.
            didaPresetManagerLog("diapreset has no resolvable sample folder; using factory synth for name="
                + up.presetName + " category=" + up.category);
        }

        didaPresetManagerLog("queued diapreset source-first load name=" + up.presetName
            + " category=" + up.category
            + " folder=" + (requestedSampleFolderPath.isNotEmpty() ? requestedSampleFolderPath
                                                                  : juce::String("<factory synth>")));

        if (onPresetLoaded) onPresetLoaded();
        else applyPendingUserDiapresetAfterSampleLoad();
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

        // Dropped/user sample folders do not carry FX metadata, so voice the
        // ambience from the folder/category to avoid inheriting muddy settings
        // from the previous preset.
        if (auto* dp = dynamic_cast<DiditagainProcessor*>(&processor))
        {
            auto& pfx = dp->getSynthEngine().getFx();
            const auto c = info.category.toLowerCase();
            if (c.contains("808") || c.contains("bass") || c.contains("sub"))
            { setParam(processor, "fxReverbMix", 0.025); setParam(processor, "fxReverbSize", 0.30); pfx.setReverbCharacter(ReverbBlock::Character::Studio); pfx.setReverbInputHighPassHz(360.0f); pfx.setReverbInputLowPassHz(4200.0f); pfx.setReverbDiffusion(0.45f); pfx.setReverbDucking(0.10f, 4.0f, 180.0f); pfx.setReverbLowMonoControl(340.0f, 0.0f); pfx.setReverbWidth(0.25f); }
            else if (c.contains("brass") || c.contains("trumpet") || c.contains("horn") || c.contains("trap"))
            { setParam(processor, "fxReverbMix", 0.11); setParam(processor, "fxReverbSize", 0.48); pfx.setReverbCharacter(ReverbBlock::Character::Trap); pfx.setReverbInputHighPassHz(240.0f); pfx.setReverbInputLowPassHz(5600.0f); pfx.setReverbDiffusion(0.54f); pfx.setReverbDucking(0.28f, 4.0f, 220.0f); pfx.setReverbLowMonoControl(320.0f, 0.03f); pfx.setReverbWidth(0.78f); }
            else if (c.contains("pad") || c.contains("choir") || c.contains("vox") || c.contains("vocal") || c.contains("string") || c.contains("texture"))
            { setParam(processor, "fxReverbMix", 0.44); setParam(processor, "fxReverbSize", 0.84); pfx.setReverbCharacter(ReverbBlock::Character::Dream); pfx.setReverbInputHighPassHz(350.0f); pfx.setReverbInputLowPassHz(7400.0f); pfx.setReverbDiffusion(0.69f); pfx.setReverbDucking(0.28f, 8.0f, 380.0f); pfx.setReverbLowMonoControl(350.0f, 0.04f); pfx.setReverbWidth(0.92f); }
            else if (c.contains("guitar"))
            { setParam(processor, "fxReverbMix", 0.24); setParam(processor, "fxReverbSize", 0.58); pfx.setReverbCharacter(ReverbBlock::Character::Vintage); pfx.setReverbInputHighPassHz(280.0f); pfx.setReverbInputLowPassHz(4800.0f); pfx.setReverbDiffusion(0.58f); pfx.setReverbDucking(0.22f, 5.0f, 260.0f); pfx.setReverbLowMonoControl(300.0f, 0.05f); pfx.setReverbWidth(0.72f); }
            else if (c.contains("lead"))
            { setParam(processor, "fxReverbMix", 0.24); setParam(processor, "fxReverbSize", 0.64); pfx.setReverbCharacter(ReverbBlock::Character::Hall); pfx.setReverbInputHighPassHz(210.0f); pfx.setReverbInputLowPassHz(8500.0f); pfx.setReverbDiffusion(0.62f); pfx.setReverbDucking(0.23f, 5.0f, 240.0f); pfx.setReverbLowMonoControl(300.0f, 0.06f); pfx.setReverbWidth(0.86f); }
            else
            { setParam(processor, "fxReverbMix", 0.18); setParam(processor, "fxReverbSize", 0.55); pfx.setReverbCharacter(ReverbBlock::Character::Studio); }
        }

        if (onPresetLoaded) onPresetLoaded();
        return;
    }

    juce::File file(info.filePath);
    loadPresetFromFile(file);
}

void PresetManager::loadPresetFromFile(const juce::File& file)
{
    requestedPresetIsUserDiapreset = false;
    pendingUserDiapresetApply = false;

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

    // Voice the premium reverb per category (V1 path).
    {
        const juce::String cat = json.getProperty(key::category, juce::String()).toString();
        if (auto* dp = dynamic_cast<DiditagainProcessor*>(&processor))
        {
            const auto c = cat.toLowerCase();
            using Ch = ReverbBlock::Character;
            Ch ch = Ch::Studio;
            if      (c.contains("bass") || c.contains("808") || c.contains("sub")) ch = Ch::Studio;
            else if (c.contains("brass") || c.contains("trumpet") || c.contains("horn") || c.contains("drill") || c.contains("trap")) ch = Ch::Trap;
            else if (c.contains("guitar"))                                          ch = Ch::Vintage;
            else if (c.contains("pad") || c.contains("string") || c.contains("texture")) ch = Ch::Dream;
            else if (c.contains("choir") || c.contains("vox") || c.contains("vocal")) ch = Ch::Cathedral;
            else if (c.contains("bell") || c.contains("pluck"))                     ch = Ch::Shimmer;
            else if (c.contains("piano") || c.contains("keys"))                     ch = Ch::Hall;
            else if (c.contains("lead"))                                            ch = Ch::Hall;
            else if (c.contains("dark") || c.contains("string"))                    ch = Ch::Dark;
            else if (c.contains("fx") || c.contains("riser"))                       ch = Ch::Cathedral;
            auto& pfx = dp->getSynthEngine().getFx();
            pfx.setReverbCharacter(ch);
            if (c.contains("bass") || c.contains("808") || c.contains("sub"))
            { pfx.setReverbInputHighPassHz(360.0f); pfx.setReverbInputLowPassHz(4200.0f); pfx.setReverbDiffusion(0.45f); pfx.setReverbDucking(0.10f, 4.0f, 180.0f); pfx.setReverbLowMonoControl(340.0f, 0.0f); pfx.setReverbWidth(0.25f); }
            else if (c.contains("brass") || c.contains("trumpet") || c.contains("horn") || c.contains("drill") || c.contains("trap"))
            { pfx.setReverbInputHighPassHz(240.0f); pfx.setReverbInputLowPassHz(5600.0f); pfx.setReverbDiffusion(0.54f); pfx.setReverbDucking(0.28f, 4.0f, 220.0f); pfx.setReverbLowMonoControl(320.0f, 0.03f); pfx.setReverbWidth(0.78f); }
            else if (c.contains("pad") || c.contains("string") || c.contains("texture"))
            { pfx.setReverbInputHighPassHz(340.0f); pfx.setReverbInputLowPassHz(7200.0f); pfx.setReverbDiffusion(0.68f); pfx.setReverbDucking(0.27f, 8.0f, 360.0f); pfx.setReverbLowMonoControl(340.0f, 0.04f); pfx.setReverbWidth(0.92f); }
            else if (c.contains("choir") || c.contains("vox") || c.contains("vocal"))
            { pfx.setReverbInputHighPassHz(360.0f); pfx.setReverbInputLowPassHz(7600.0f); pfx.setReverbDiffusion(0.70f); pfx.setReverbDucking(0.28f, 9.0f, 390.0f); pfx.setReverbLowMonoControl(350.0f, 0.04f); pfx.setReverbWidth(0.94f); }
            else if (c.contains("guitar"))
            { pfx.setReverbInputHighPassHz(280.0f); pfx.setReverbInputLowPassHz(4800.0f); pfx.setReverbDiffusion(0.58f); pfx.setReverbDucking(0.22f, 5.0f, 260.0f); pfx.setReverbLowMonoControl(300.0f, 0.05f); pfx.setReverbWidth(0.72f); }
            else if (c.contains("lead"))
            { pfx.setReverbInputHighPassHz(210.0f); pfx.setReverbInputLowPassHz(8500.0f); pfx.setReverbDiffusion(0.62f); pfx.setReverbDucking(0.23f, 5.0f, 240.0f); pfx.setReverbLowMonoControl(300.0f, 0.06f); pfx.setReverbWidth(0.86f); }
        }
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

void PresetManager::loadDiapresetFiles()
{
    // Scan <Samples>/Presets/User/<Category>/**/*.diapreset and add one entry
    // per file. The immediate folder under User is always the browser category,
    // so nested instrument/source folders never become UI subcategories.
    auto root = getUserPresetDirectory();
    if (! root.isDirectory()) return;

    auto categoryDirs = root.findChildFiles(juce::File::findDirectories, false);
    std::sort(categoryDirs.begin(), categoryDirs.end(),
        [](const juce::File& a, const juce::File& b)
        { return a.getFileName().compareNatural(b.getFileName()) < 0; });

    for (auto& catDir : categoryDirs)
    {
        const auto cat = catDir.getFileName();
        auto files = catDir.findChildFiles(juce::File::findFiles, true, "*.diapreset");
        std::sort(files.begin(), files.end(),
            [](const juce::File& a, const juce::File& b)
            { return a.getFileName().compareNatural(b.getFileName()) < 0; });

        for (auto& f : files)
        {
            dida::userpreset::UserPreset up;
            juce::String err;
            if (! dida::userpreset::parseFile(f, up, err))
            {
                didaPresetManagerLog("skipping invalid diapreset file=" + f.getFullPathName() + " err=" + err);
                continue;
            }

            PresetInfo info;
            info.name           = up.presetName;
            info.author         = "User";
            info.category       = cat;
            info.description    = "User preset (" + up.source.type + ")";
            info.filePath       = f.getFullPathName();
            info.isFactory      = false;
            info.isSampleDrop   = false;
            info.isUserPreset   = true;
            info.userPresetFile = f.getFullPathName();
            presets.push_back(info);
        }
    }
}

void PresetManager::seedGuitarPresetBankIfMissing()
{
    auto root = getUserPresetDirectory();
    auto guitarsDir = root.getChildFile("Guitars");
    guitarsDir.createDirectory();

    // One-time seed marker. Once this file exists, we never re-seed the bank,
    // so any presets the user deletes stay deleted across sessions.
    auto seededMarker = guitarsDir.getChildFile(".seeded");
    if (seededMarker.existsAsFile())
        return;

    // Default source folder: <Samples>/Guitars/Guitar 1. We seed against the
    // actual on-disk path so users get a working bank out of the box.
    auto sourceFolder = dida::SampleLibrary::getSamplesRoot()
                            .getChildFile("Guitars").getChildFile("Guitar 1");

    const auto srcPath = sourceFolder.getFullPathName().replaceCharacter('\\', '/');
    auto bank = dida::userpreset::buildGuitarBank(srcPath);

    int written = 0;
    for (auto& p : bank)
    {
        auto file = guitarsDir.getChildFile(p.presetName + ".diapreset");
        if (file.existsAsFile())
            continue;
        file.replaceWithText(dida::userpreset::toJson(p));
        ++written;
    }

    seededMarker.replaceWithText("1");

    if (written > 0)
        didaPresetManagerLog("seeded guitar preset bank count=" + juce::String(written)
            + " dir=" + guitarsDir.getFullPathName());
}

//==============================================================================
// One-time seed of the 20 Vintage Synth presets. Mirrors the guitar bank
// pattern: writes into <UserPresets>/VintageSynth/ behind a .seeded marker
// so user deletions are persistent across sessions.
//==============================================================================
void PresetManager::seedVintageSynthBankIfMissing()
{
    auto root = getUserPresetDirectory();
    auto dir  = root.getChildFile("VintageSynth");
    dir.createDirectory();

    auto seededMarker = dir.getChildFile(".seeded");
    if (seededMarker.existsAsFile())
        return;

    // Use Synths/Lead 1 as the base sample source (matches preset spec).
    auto sourceFolder = dida::SampleLibrary::getSamplesRoot()
                            .getChildFile("Synths").getChildFile("Lead 1");

    const auto srcPath = sourceFolder.getFullPathName().replaceCharacter('\\', '/');
    auto bank = dida::userpreset::buildVintageSynthBank(srcPath);

    int written = 0;
    for (auto& p : bank)
    {
        auto file = dir.getChildFile(p.presetName + ".diapreset");
        if (file.existsAsFile())
            continue;
        file.replaceWithText(dida::userpreset::toJson(p));
        ++written;
    }

    seededMarker.replaceWithText("1");

    if (written > 0)
        didaPresetManagerLog("seeded vintage synth bank count=" + juce::String(written)
            + " dir=" + dir.getFullPathName());
}

//==============================================================================
// autoIndexUserInstrumentFolders
//
// User layout reality (per their screenshot):
//   <Samples>/Presets/User/<Category>/<Instrument>/<root-mapped>.wav
//   <Samples>/Presets/User/<Category>/<*.wav>                  (flat)
//
// The browser already lists every `.diapreset` from `loadDiapresetFiles`. That
// alone leaves freshly-added instrument folders invisible — the user has to
// hand-author a `.diapreset` for each one. This pass closes the gap:
//
//   * Any subfolder under a category that contains at least one root-mapped
//     WAV becomes a playable preset entry (sample-drop style) in the same
//     category, named after the folder leaf.
//   * If a category folder itself has root-mapped WAVs directly inside it,
//     the category folder is exposed as one preset named after the category.
//   * Categories with no recognisable WAV content are skipped (the
//     `.diapreset` files inside still show via the earlier pass).
//   * We de-dupe against entries already added by `loadDiapresetFiles`
//     (same category + same name) so a curated `.diapreset` always wins.
//
// Net effect: drop a new folder of mapped WAVs under
// `Presets/User/<Category>/` and it shows up in the browser on next rescan
// with zero JSON authoring.
//==============================================================================
void PresetManager::autoIndexUserInstrumentFolders()
{
    auto root = getUserPresetDirectory();
    if (! root.isDirectory()) return;

    auto countMappedWavs = [](const juce::File& dir, bool recursive) -> int
    {
        if (! dir.isDirectory()) return 0;
        auto wavs = dir.findChildFiles(juce::File::findFiles, recursive, "*.wav");
        int n = 0;
        for (auto& w : wavs)
            if (parseRootMidiFromStem(w.getFileNameWithoutExtension()) >= 0)
                ++n;
        return n;
    };

    auto findRepresentativeWav = [](const juce::File& dir, int& outRoot,
                                    juce::StringArray& outAll) -> juce::File
    {
        outRoot = 60;
        outAll.clear();
        auto wavs = dir.findChildFiles(juce::File::findFiles, true, "*.wav");
        std::sort(wavs.begin(), wavs.end(),
            [](const juce::File& a, const juce::File& b)
            { return a.getFileName().compareNatural(b.getFileName()) < 0; });

        juce::Array<juce::File> mapped;
        for (auto& w : wavs)
            if (parseRootMidiFromStem(w.getFileNameWithoutExtension()) >= 0)
            { mapped.add(w); outAll.add(w.getFullPathName()); }

        if (mapped.isEmpty()) return {};
        const auto rep = chooseRepresentativeMappedWav(mapped, outRoot);
        if (outAll.size() < 2) outAll.clear();
        return rep;
    };

    auto alreadyHasEntry = [this](const juce::String& cat, const juce::String& name) -> bool
    {
        for (const auto& p : presets)
            if (p.category.equalsIgnoreCase(cat) && p.name.equalsIgnoreCase(name))
                return true;
        return false;
    };

    auto addEntry = [&](const juce::String& cat, const juce::String& name,
                        const juce::File& folder)
    {
        int rep = 60;
        juce::StringArray allPaths;
        const auto chosen = findRepresentativeWav(folder, rep, allPaths);
        if (! chosen.existsAsFile()) return;

        PresetInfo info;
        info.name              = name;
        info.author            = "User";
        info.category          = cat;
        info.description       = juce::String(allPaths.isEmpty() ? 1 : allPaths.size()) + " samples";
        info.filePath          = chosen.getFullPathName();
        info.isFactory         = false;
        info.isSampleDrop      = true;
        info.sampleSourcePath  = chosen.getFullPathName();
        info.sampleSourcePaths = allPaths;
        info.sampleFolderPath  = folder.getFullPathName();
        info.sampleRootMidi    = rep;
        info.sampleLooping     = isSustainedSampleCategory(cat);
        presets.push_back(info);
    };

    int added = 0;
    auto categoryDirs = root.findChildFiles(juce::File::findDirectories, false);
    std::sort(categoryDirs.begin(), categoryDirs.end(),
        [](const juce::File& a, const juce::File& b)
        { return a.getFileName().compareNatural(b.getFileName()) < 0; });

    for (auto& catDir : categoryDirs)
    {
        const auto cat = catDir.getFileName();

        // 1) Flat layout: WAVs directly in the category folder -> one preset
        //    named after the category itself.
        if (countMappedWavs(catDir, false) > 0)
        {
            if (! alreadyHasEntry(cat, cat))
            { addEntry(cat, cat, catDir); ++added; }
        }

        // 2) Nested layout: each subfolder with mapped WAVs becomes its own
        //    preset (e.g. Brass/TRUMPET 1, Saxophone/Alto Sax).
        auto subdirs = catDir.findChildFiles(juce::File::findDirectories, false);
        std::sort(subdirs.begin(), subdirs.end(),
            [](const juce::File& a, const juce::File& b)
            { return a.getFileName().compareNatural(b.getFileName()) < 0; });

        for (auto& sub : subdirs)
        {
            if (countMappedWavs(sub, true) <= 0) continue;
            const auto name = sub.getFileName();
            if (alreadyHasEntry(cat, name)) continue;
            addEntry(cat, name, sub);
            ++added;
        }
    }

    juce::Logger::writeToLog(juce::String("[DIDITAGAIN browser] autoIndexed instrument folders=")
        + juce::String(added));
}

