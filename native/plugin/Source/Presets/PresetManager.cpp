#include "PresetManager.h"
#include "PresetSchema.h"
#include "FactoryPresets.h"
#include "HybridPresetV2.h"
#include "PresetMigration.h"
#include "HybridPresetApplier.h"
#include "PresetQualityReport.h"
#include "SourceFolderValidator.h"
#include "UserPresetLoader.h"
#include "UserPresetFormat.h"
#include "GuitarPresetBank.h"
#include "VintageSynthBank.h"
#include "../DSP/SampleLibrary.h"
#include "../PluginProcessor.h"
#include "../DSP/SynthEngine.h"
#include "BinaryData.h"
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
    seedAiTextureDemoPackIfMissing();

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

    // Read-only hidden source-folder validation pass. Writes
    // Logs/source_folder_validation.{json,txt} and emits one
    // "[DIDITAGAIN source-validator]" line per folder. No audio behaviour.
    dida::sourcevalidator::validateAll();
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

static int countWavFiles(const juce::File& folder)
{
    return folder.isDirectory()
        ? folder.findChildFiles(juce::File::findFiles, true, "*.wav").size()
        : 0;
}

static juce::String exactAiTextureBaseRelativePath(const dida::userpreset::UserPreset& up)
{
    const auto name = up.presetName.toLowerCase();
    const auto cat  = up.category.toLowerCase();
    const auto raw  = up.source.path.replaceCharacter('\\', '/').toLowerCase();
    if (name.contains("brass") || cat.contains("brass") || raw.contains("/brass") || raw.startsWith("brass"))
        return "Presets/User/Brass/Brass 1";
    if (name.contains("choir") || cat.contains("choir") || cat.contains("vox")
        || raw.contains("/choir") || raw.startsWith("choir"))
        return "Presets/User/Choir Ohhh";
    if (name.contains("guitar") || cat.contains("guitar") || raw.contains("/guitar") || raw.startsWith("guitar"))
        return "Presets/User/Acoustic Guitars/Acoustic Guitar 1";
    return {};
}

static juce::File resolveAiTextureBaseSourceCandidate(const juce::String& rawPath,
                                                      const dida::userpreset::UserPreset& up)
{
    auto src = rawPath.replaceCharacter('\\', '/').trim();
    while ((src.startsWithChar('"') && src.endsWithChar('"'))
        || (src.startsWithChar('\'') && src.endsWithChar('\'')))
        src = src.substring(1, src.length() - 1).trim();
    while (src.endsWithChar('/') && src.length() > 1)
        src = src.dropLastCharacters(1);
    if (src.isEmpty()) return {};

    const auto samplesRoot = dida::SampleLibrary::getSamplesRoot();
    const auto docsRoot = samplesRoot.getParentDirectory();
    const auto docsPath = docsRoot.getFullPathName().replaceCharacter('\\', '/');
    const auto samplesPath = samplesRoot.getFullPathName().replaceCharacter('\\', '/');
    // AI Texture base multisamples live under the user preset tree:
    //   <Documents>/DIDITAGAIN STUDIO/Samples/Presets/User/<...>
    const auto presetsUserRoot = samplesRoot.getChildFile("Presets").getChildFile("User");

    auto expanded = src;
    expanded = expanded.replace("{DIDA_DOCS}", docsPath, true)
                       .replace("{DocsRoot}", docsPath, true)
                       .replace("{Docs}", docsPath, true)
                       .replace("{Documents}", docsPath, true)
                       .replace("{Samples}", samplesPath, true);

    auto candidate = juce::File::isAbsolutePath(expanded)
        ? juce::File(expanded)
        : (expanded.startsWithIgnoreCase("Samples/")
            ? samplesRoot.getChildFile(expanded.substring(8))
            : samplesRoot.getChildFile(expanded));

    // STEP C resolution: first honour the preset's exact sourceInstrument.path
    // after token expansion. Only when that exact path fails do we retry a
    // relative tail under <Samples>/Presets/User and, finally, a known legacy
    // broad-category path mapped to the current Presets/User multisample.
    if (! candidate.isDirectory())
    {
        auto rel = expanded;
        if (rel.startsWithIgnoreCase("Samples/Presets/User/"))
            rel = rel.substring(juce::String("Samples/Presets/User/").length());
        else if (rel.startsWithIgnoreCase("Samples/"))
            rel = rel.substring(8);

        const auto marker = juce::String("/Samples/Presets/User/");
        const int idx = expanded.indexOfIgnoreCase(marker);
        if (idx >= 0)
            rel = expanded.substring(idx + marker.length());

        rel = rel.trim();
        while (rel.endsWithChar('/') && rel.length() > 1)
            rel = rel.dropLastCharacters(1);

        if (rel.isNotEmpty())
        {
            auto userCandidate = presetsUserRoot.getChildFile(rel);
            if (userCandidate.isDirectory())
                candidate = userCandidate;
        }
    }

    const auto exactRel = exactAiTextureBaseRelativePath(up);
    const auto srcNorm = src.replaceCharacter('\\', '/');
    const auto expandedNorm = expanded.replaceCharacter('\\', '/');
    if (! candidate.isDirectory()
        && exactRel.isNotEmpty()
        && (srcNorm.equalsIgnoreCase("Brass")
            || srcNorm.equalsIgnoreCase("Choir")
            || srcNorm.equalsIgnoreCase("Choirs")
            || srcNorm.equalsIgnoreCase("ChoirsVox")
            || srcNorm.equalsIgnoreCase("Guitar")
            || srcNorm.equalsIgnoreCase("Guitars")
            || srcNorm.endsWithIgnoreCase("/Samples/Brass")
            || srcNorm.endsWithIgnoreCase("/Samples/Choir")
            || srcNorm.endsWithIgnoreCase("/Samples/Choirs")
            || srcNorm.endsWithIgnoreCase("/Samples/ChoirsVox")
            || srcNorm.endsWithIgnoreCase("/Samples/Guitar")
            || srcNorm.endsWithIgnoreCase("/Samples/Guitars")
            || expandedNorm.endsWithIgnoreCase("/Samples/Brass")
            || expandedNorm.endsWithIgnoreCase("/Samples/Choir")
            || expandedNorm.endsWithIgnoreCase("/Samples/Choirs")
            || expandedNorm.endsWithIgnoreCase("/Samples/ChoirsVox")
            || expandedNorm.endsWithIgnoreCase("/Samples/Guitar")
            || expandedNorm.endsWithIgnoreCase("/Samples/Guitars")))
        candidate = samplesRoot.getChildFile(exactRel);

    return candidate;
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

[[maybe_unused]] static juce::File findCategorySourceFolder(const juce::File& userPresetRoot,
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

    // 2b) Disambiguate by preset-name keyword. When the category contains
    //     several named instrument subfolders (e.g. Guitars/Acoustic vs
    //     Guitars/Electric, Brass/Trumpet vs Brass/Trombone), route presets
    //     whose name mentions a subfolder's name into that subfolder.
    const auto presetStem = presetFile.getFileNameWithoutExtension().toLowerCase();
    if (presetStem.isNotEmpty())
    {
        juce::File bestMatch;
        int bestLen = 0;
        for (auto& sub : subdirs)
        {
            const auto leaf = sub.getFileName().toLowerCase().trim();
            if (leaf.isEmpty()) continue;
            // Strip a trailing " <n>" or "<n>" from subfolder names
            // ("Acoustic 1" -> "acoustic") so name-keyword matches still work.
            auto stem = leaf;
            while (stem.isNotEmpty() && (juce::CharacterFunctions::isDigit(stem.getLastCharacter())
                                         || stem.getLastCharacter() == ' '))
                stem = stem.dropLastCharacters(1);
            if (stem.length() < 4) continue;            // skip "1", "alt" noise
            if (! presetStem.containsWholeWord(stem)) continue;
            if (! folderHasAnyAudio(sub, true)) continue;
            if (stem.length() > bestLen) { bestMatch = sub; bestLen = stem.length(); }
        }
        if (bestMatch.isDirectory()) return bestMatch;
    }

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

// Expected hidden source folder name(s) for a given category. Used by the
// strict per-category resolver so e.g. "Acoustic Guitars" never accidentally
// reaches into "Electric Guitars/electric guitar 1".
static juce::StringArray expectedSourceFolderNames(const juce::String& categoryIn)
{
    const auto c = categoryIn.trim();
    auto eq = [&](const char* s){ return c.equalsIgnoreCase(s); };
    if (eq("Acoustic Guitars") || eq("Acoustic Guitar"))      return { "Acoustic Guitar 1" };
    if (eq("Electric Guitars") || eq("Electric Guitar"))      return { "Electric Guitar 1", "electric guitar 1", "Guitar 1" };
    if (eq("Guitars") || eq("Guitar"))                        return { "Guitar 1" };
    if (eq("Pianos") || eq("Piano"))                          return { "Piano 1" };
    if (eq("Rhodes"))                                         return { "Rhodes 1" };
    if (eq("Brass") || eq("Trap Trumpets") || eq("Trumpet")
        || eq("Trumpets"))                                    return { "Trumpet 1" };
    if (eq("Saxophone") || eq("Saxophones") || eq("Saxaphone")
        || eq("Saxaphones"))                                  return { "Saxophone 1", "Saxaphone 1" };
    if (eq("Cellos") || eq("Cello"))                          return { "Cello 1" };
    if (eq("Strings"))                                        return { "Strings 1", "Cello 1" };
    if (eq("Choirs") || eq("Choir"))                          return { "Choir 1", "choir 1" };
    if (eq("Bells") || eq("Bell"))                            return { "Bell 1" };
    if (eq("808s") || eq("808"))                              return { "808 1" };
    if (eq("Leads") || eq("Lead"))                            return { "Lead 1" };
    if (eq("Synths") || eq("Synth"))                          return { "Lead 1", "Vintage Synth 1" };
    if (eq("VintageSynth") || eq("Vintage Synths") || eq("Vintage Synth"))
                                                              return { "Vintage Synth 1" };
    return {};
}

// Strict per-category source resolver. Looks ONLY at immediate subdirectories
// of presetCategoryFolder; never crosses into other categories.
//
//   1) Try every name in expectedSourceFolderNames(category).
//   2) Try preferredLeaf (from sourceInstrument.path) if it lives directly
//      under presetCategoryFolder.
//   3) Fallback: any single subdirectory that contains audio.
//
// Sets `multipleFoundOut=true` when several subdirectories contain audio and
// no expected-name match resolved (so the caller can warn).
static juce::File findStrictCategorySourceFolder(const juce::File& presetCategoryFolder,
                                                 const juce::String& category,
                                                 const juce::String& preferredLeaf,
                                                 bool& multipleFoundOut)
{
    multipleFoundOut = false;
    if (! presetCategoryFolder.isDirectory()) return {};

    auto subdirs = presetCategoryFolder.findChildFiles(juce::File::findDirectories, false);

    const auto expected = expectedSourceFolderNames(category);
    for (auto& name : expected)
        for (auto& sub : subdirs)
            if (sub.getFileName().equalsIgnoreCase(name) && folderHasAnyAudio(sub, true))
                return sub;

    if (preferredLeaf.isNotEmpty())
        for (auto& sub : subdirs)
            if (sub.getFileName().equalsIgnoreCase(preferredLeaf)
                && folderHasAnyAudio(sub, true))
                return sub;

    juce::Array<juce::File> withAudio;
    for (auto& sub : subdirs)
        if (folderHasAnyAudio(sub, true))
            withAudio.add(sub);

    if (withAudio.size() == 1) return withAudio.getFirst();
    if (withAudio.size() > 1)
    {
        multipleFoundOut = true;
        std::sort(withAudio.begin(), withAudio.end(), [](const juce::File& a, const juce::File& b) {
            return a.getFileName().compareNatural(b.getFileName()) < 0;
        });
        return withAudio.getFirst();
    }
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

    // NOTE: subfolders of <Samples>/Presets/User/<Category>/ are intentionally
    // NOT exposed as browser presets. They are "hidden source folders" used
    // only by the .diapreset loader to resolve sourceInstrument.path. The
    // browser shows .diapreset files only (see loadDiapresetFiles()).

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
    didaPresetManagerLog("applying diapreset sound design after source load: " + pendingUserDiapreset.presetName);

    // Task 4: category reverb defaults are applied FIRST so the preset can have
    // the final say. applyToProcessor() (called below) re-applies the category
    // character internally and then pushes any explicit preset reverb overrides
    // on top, so these baseline defaults must not run afterwards.
    if (auto* dp = dynamic_cast<DiditagainProcessor*>(&processor))
    {
        auto& pfx = dp->getSynthEngine().getFx();
        const auto c = pendingUserDiapreset.category.toLowerCase();
        if (c.contains("808") || c.contains("bass") || c.contains("sub"))
        { pfx.setReverbInputHighPassHz(360.0f); pfx.setReverbInputLowPassHz(4200.0f); pfx.setReverbDiffusion(0.45f); pfx.setReverbDucking(0.10f, 4.0f, 180.0f); pfx.setReverbLowMonoControl(340.0f, 0.0f); pfx.setReverbWidth(0.25f); }
        else if (c.contains("brass") || c.contains("trumpet") || c.contains("horn") || c.contains("trap"))
        { pfx.setReverbInputHighPassHz(240.0f); pfx.setReverbInputLowPassHz(5600.0f); pfx.setReverbDiffusion(0.54f); pfx.setReverbDucking(0.28f, 4.0f, 220.0f); pfx.setReverbLowMonoControl(320.0f, 0.03f); pfx.setReverbWidth(0.78f); }
        else if (pendingUserDiapreset.choirMode || c.contains("choir") || c.contains("vox") || c.contains("vocal"))
        { pfx.setReverbInputHighPassHz(300.0f); pfx.setReverbInputLowPassHz(5500.0f); pfx.setReverbDiffusion(0.69f); pfx.setReverbDucking(0.32f, 6.0f, 220.0f); pfx.setDelayDucking(0.50f, 5.0f, 140.0f); pfx.setNoteDensityFxReductionEnabled(true); pfx.setNoteDensityMaxReduction(0.35f); pfx.setDelayDensityWeight(1.0f); pfx.setReverbDensityWeight(0.75f); pfx.setChoirDensityMode(true); pfx.setReverbLowMonoControl(300.0f, 0.0f); pfx.setReverbWidth(0.90f); }
        else if (c.contains("pad") || c.contains("string") || c.contains("texture"))
        { pfx.setReverbInputHighPassHz(350.0f); pfx.setReverbInputLowPassHz(7400.0f); pfx.setReverbDiffusion(0.69f); pfx.setReverbDucking(0.28f, 8.0f, 380.0f); pfx.setReverbLowMonoControl(350.0f, 0.04f); pfx.setReverbWidth(0.92f); }
        else if (c.contains("guitar"))
        { pfx.setReverbInputHighPassHz(280.0f); pfx.setReverbInputLowPassHz(4800.0f); pfx.setReverbDiffusion(0.58f); pfx.setReverbDucking(0.22f, 5.0f, 260.0f); pfx.setReverbLowMonoControl(300.0f, 0.05f); pfx.setReverbWidth(0.72f); }
        else if (c.contains("lead"))
        { pfx.setReverbInputHighPassHz(210.0f); pfx.setReverbInputLowPassHz(8500.0f); pfx.setReverbDiffusion(0.62f); pfx.setReverbDucking(0.23f, 5.0f, 240.0f); pfx.setReverbLowMonoControl(300.0f, 0.06f); pfx.setReverbWidth(0.86f); }
        else
        { pfx.setReverbCharacter(ReverbBlock::Character::Studio); }
    }

    // Preset has the final say: this applies the category character again and
    // then layers explicit preset reverb/FX overrides on top of the defaults.
    dida::userpreset::applyToProcessor(pendingUserDiapreset, processor);
    logFinalActivePresetParams(processor, pendingUserDiapreset.presetName);

    if (auto* dp = dynamic_cast<DiditagainProcessor*>(&processor))
    {
        qualityReportWaitingForRender = true;
        qualityReportPendingLoadId = dp->getCurrentPresetLoadIdForReport();
        qualityReportFinalEmittedLoadId = 0;
        emitCurrentUserDiapresetQualityReport(); // pending: tells the user to play a note first
    }
}

void PresetManager::emitCurrentUserDiapresetQualityReport()
{
    if (auto* dp = dynamic_cast<DiditagainProcessor*>(&processor))
        dida::presetreport::report(*dp, pendingUserDiapreset, requestedCategory,
                                   requestedSampleFolderPath,
                                   requestedSampleSources.size(),
                                   requestedSampleRawPath,
                                   requestedSampleResolvedFrom,
                                   requestedSampleRawInsidePresetsUser,
                                   /*browserPresetName*/ {},
                                   /*bankCategory*/ {},
                                   requestedPresetFilePath,
                                   requestedPresetCategoryFolder,
                                   requestedExpectedSourceFolderName,
                                   requestedAllowCrossCategorySource,
                                   requestedSourceFolderWavCount,
                                    requestedAiTextureBaseSourceRaw,
                                    requestedAiTextureBaseSourceResolvedCandidate,
                                    requestedAiTextureBaseSourceExists,
                                    requestedAiTextureBaseSourceWavCount,
                                   requestedExtraSourceWarnings);
}

void PresetManager::emitPendingUserDiapresetQualityReportIfReady()
{
    if (! qualityReportWaitingForRender)
        return;
    auto* dp = dynamic_cast<DiditagainProcessor*>(&processor);
    if (dp == nullptr)
        return;
    const int currentLoadId = dp->getCurrentPresetLoadIdForReport();
    if (qualityReportPendingLoadId <= 0 || currentLoadId != qualityReportPendingLoadId)
        return;
    if (qualityReportFinalEmittedLoadId == currentLoadId)
        return;
    if (dp->getLastRenderedPresetLoadId() != currentLoadId)
        return;

    qualityReportFinalEmittedLoadId = currentLoadId;
    qualityReportWaitingForRender = false;
    emitCurrentUserDiapresetQualityReport();
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

int PresetManager::findDefaultPianoPresetIndex() const
{
    const int n = static_cast<int>(presets.size());

    // 1. First non-factory .diapreset in category "Pianos".
    for (int i = 0; i < n; ++i)
    {
        const auto& p = presets[i];
        if (! p.isFactory && p.isUserPreset && p.category.equalsIgnoreCase("Pianos"))
            return i;
    }
    // 2. First sample-drop preset in category "Pianos".
    for (int i = 0; i < n; ++i)
    {
        const auto& p = presets[i];
        if (p.isSampleDrop && p.category.equalsIgnoreCase("Pianos"))
            return i;
    }
    // 3. Any preset in category "Pianos".
    for (int i = 0; i < n; ++i)
        if (presets[i].category.equalsIgnoreCase("Pianos"))
            return i;
    // 4. First preset whose name/category contains "piano".
    for (int i = 0; i < n; ++i)
    {
        if (presets[i].name.containsIgnoreCase("piano")
            || presets[i].category.containsIgnoreCase("piano"))
            return i;
    }
    return -1;
}

int PresetManager::findPresetIndexByIdentity(const juce::String& userPresetFile,
                                             const juce::String& filePath,
                                             const juce::String& name,
                                             const juce::String& category,
                                             int fallbackIndex) const
{
    const int n = static_cast<int>(presets.size());

    // 1. Exact .diapreset / user preset file path match.
    if (userPresetFile.isNotEmpty())
        for (int i = 0; i < n; ++i)
            if (presets[i].userPresetFile == userPresetFile)
                return i;

    // 1b. Exact preset file path match.
    if (filePath.isNotEmpty())
        for (int i = 0; i < n; ++i)
            if (presets[i].filePath == filePath)
                return i;

    // 2. Exact name + category match.
    if (name.isNotEmpty())
        for (int i = 0; i < n; ++i)
            if (presets[i].name.equalsIgnoreCase(name)
                && presets[i].category.equalsIgnoreCase(category))
                return i;

    // 2b. Name only.
    if (name.isNotEmpty())
        for (int i = 0; i < n; ++i)
            if (presets[i].name.equalsIgnoreCase(name))
                return i;

    // 3. Saved index as last fallback.
    if (fallbackIndex >= 0 && fallbackIndex < n)
        return fallbackIndex;

    return -1;
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

        const auto rawSourcePath = up.source.path.trim();
        const auto rawNormSlash  = rawSourcePath.replaceCharacter('\\', '/');
        const bool rawIsAbsolute = rawSourcePath.isNotEmpty()
                                && juce::File::isAbsolutePath(rawNormSlash);
        const bool rawIsInsidePresetsUser = rawNormSlash.containsIgnoreCase("/Samples/Presets/User/");
        const bool aiTexturePresetForRouting = dida::userpreset::isAiTexturePreset(up);

        // The .diapreset's parent folder is the default authoritative category.
        // EXCEPTION: when sourceInstrument.path explicitly points inside a
        // different "Presets/User/<OtherCategory>/..." folder that actually
        // exists on disk, treat THAT folder as the authoritative category.
        // This prevents e.g. an "Acoustic Guitar" preset that was dropped into
        // the wrong Guitars folder from being normalised down to "Guitars" and
        // routed to "Guitar 1".
        auto parentFolderName = file.getParentDirectory().getFileName();
        juce::File presetCategoryFolder = file.getParentDirectory();

        if (rawIsInsidePresetsUser)
        {
            const int marker = rawNormSlash.indexOfIgnoreCase("/Samples/Presets/User/");
            if (marker >= 0)
            {
                const auto tail = rawNormSlash.substring(marker + juce::String("/Samples/Presets/User/").length());
                const int slash = tail.indexOfChar('/');
                const auto otherCat = (slash > 0 ? tail.substring(0, slash) : tail).trim();
                if (otherCat.isNotEmpty()
                    && ! otherCat.equalsIgnoreCase(parentFolderName))
                {
                    auto candidate = file.getParentDirectory().getParentDirectory().getChildFile(otherCat);
                    if (candidate.isDirectory())
                    {
                        didaPresetManagerLog("diapreset category overridden by sourceInstrument.path"
                            " from=" + parentFolderName + " to=" + otherCat
                            + " file=" + file.getFullPathName());
                        presetCategoryFolder = candidate;
                        parentFolderName = otherCat;
                    }
                }
            }
        }

        const auto effectiveCategoryRaw = parentFolderName.isNotEmpty()
            ? parentFolderName
            : (info.category.isNotEmpty() ? info.category
                : (up.category.isNotEmpty() ? up.category : juce::String("User")));
        const auto effectiveCategory = normalizeCategoryAlias(effectiveCategoryRaw);
        const auto sourceLeaf = juce::File(up.source.path.replaceCharacter('\\', '/')).getFileName();

        // STRICT per-category routing. We only search inside presetCategoryFolder
        // and never reach across categories.
        const auto expectedNames = expectedSourceFolderNames(effectiveCategory);
        const juce::String expectedSourceFolderName =
            expectedNames.isEmpty() ? juce::String() : expectedNames[0];
        const bool allowCrossCategorySource = false;

        juce::File resolved;
        juce::String resolvedFrom;
        juce::StringArray extraSourceWarnings;

        juce::String aiTextureBaseSourceRaw;
        juce::String aiTextureBaseSourceResolvedCandidate;
        bool aiTextureBaseSourceExists = false;
        int aiTextureBaseSourceWavCount = 0;

        // STEP A — search ONLY inside the .diapreset's own category folder.
        // AI Texture presets are routed by STEP C instead: their source path is
        // a real base multisample under <Documents>/DIDITAGAIN STUDIO/Samples,
        // not a hidden source folder beside the preset JSON.
        bool multipleFound = false;
        if (! aiTexturePresetForRouting)
        {
            auto picked = findStrictCategorySourceFolder(presetCategoryFolder,
                                                        effectiveCategory,
                                                        sourceLeaf,
                                                        multipleFound);
            if (picked.isDirectory())
            {
                resolved = picked;
                resolvedFrom = "categoryHiddenSourceFolder";
                didaPresetManagerLog("diapreset routed strict category=" + effectiveCategory
                    + " folder=" + resolved.getFullPathName());
                if (multipleFound
                    && (expectedSourceFolderName.isEmpty()
                        || ! picked.getFileName().equalsIgnoreCase(expectedSourceFolderName)))
                    extraSourceWarnings.add("MULTIPLE_SOURCE_FOLDERS_FOUND");
            }
        }

        // STEP B — honour an absolute sourceInstrument.path only if it points
        // INSIDE the same category folder. Otherwise reject as cross-category.
        if (rawIsAbsolute && ! aiTexturePresetForRouting)
        {
            auto abs = dida::userpreset::resolveSourcePath(rawSourcePath);
            if (abs.isDirectory())
            {
                const bool isUnderCatFolder = abs.isAChildOf(presetCategoryFolder)
                                           || abs == presetCategoryFolder;
                if (isUnderCatFolder)
                {
                    if (! resolved.isDirectory())
                    {
                        resolved = abs;
                        resolvedFrom = "absoluteSourceInstrumentPath";
                        didaPresetManagerLog("diapreset using absolute source path=" + resolved.getFullPathName());
                    }
                }
                else if (! allowCrossCategorySource && ! resolved.isDirectory())
                {
                    // BUG 6: before declaring a cross-category routing failure,
                    // accept the absolute folder when its OWN category normalises
                    // to the same alias as this preset's category (e.g. a
                    // "Saxaphone" folder for a "Saxophone" preset). Folder
                    // structure is not changed — only the matching is alias-aware.
                    const auto absCatRaw = abs.getParentDirectory().getFileName();
                    const bool sameAliasCategory =
                        normalizeCategoryAlias(absCatRaw).equalsIgnoreCase(effectiveCategory)
                        || normalizeCategoryAlias(abs.getFileName()).equalsIgnoreCase(effectiveCategory);
                    if (sameAliasCategory)
                    {
                        resolved = abs;
                        resolvedFrom = "aliasCategorySourceFolder";
                        didaPresetManagerLog(juce::String("diapreset accepted alias-category source")
                            + " presetName=" + up.presetName
                            + " category=" + effectiveCategory
                            + " resolvedFolder=" + abs.getFullPathName()
                            + " reason=categoryAliasMatch");
                    }
                    else
                    {
                        // Only a real routing failure: STEP A did NOT already find
                        // a valid in-category source folder, and the absolute path
                        // points outside this category under a different alias.
                        extraSourceWarnings.add("WRONG_CATEGORY_SOURCE_FOLDER");
                        didaPresetManagerLog(juce::String("diapreset REJECTED cross-category source")
                            + " presetName=" + up.presetName
                            + " category=" + effectiveCategory
                            + " presetFilePath=" + file.getFullPathName()
                            + " sourceInstrumentPathRaw=" + rawSourcePath
                            + " presetCategoryFolder=" + presetCategoryFolder.getFullPathName()
                            + " attemptedResolvedFolder=" + abs.getFullPathName()
                            + " expectedSourceFolderName=" + expectedSourceFolderName
                            + " reason=rejectedCrossCategorySource");
                    }
                }
            }
        }

        // STEP C — AI Texture enhancement presets use a real base multisample
        // folder under <Documents>/DIDITAGAIN STUDIO/Samples as the main body.
        // Resolve relative paths like "Brass/Brass 1" directly against Samples,
        // and normalize old broad {DIDA_DOCS}/Samples/<Category> references to
        // the exact shipped instrument folder. Scoped to AI Texture only.
        if (aiTexturePresetForRouting && rawSourcePath.isNotEmpty())
        {
            aiTextureBaseSourceRaw = rawSourcePath;
            auto baseFolder = resolveAiTextureBaseSourceCandidate(rawSourcePath, up);
            aiTextureBaseSourceResolvedCandidate = baseFolder.getFullPathName().replaceCharacter('\\', '/');
            aiTextureBaseSourceExists = baseFolder.isDirectory();
            aiTextureBaseSourceWavCount = countWavFiles(baseFolder);

            if (aiTextureBaseSourceExists && aiTextureBaseSourceWavCount > 0)
            {
                resolved = baseFolder;
                resolvedFrom = "aiTextureBaseMultisample";
                didaPresetManagerLog("AI Texture preset using base multisample folder="
                    + resolved.getFullPathName() + " rawPath=" + rawSourcePath
                    + " wavCount=" + juce::String(aiTextureBaseSourceWavCount)
                    + " name=" + up.presetName);
            }
        }

        const bool sourceRequiredForEngine = dida::presetreport::engineRequiresSource(up);

        if (! resolved.isDirectory() && sourceRequiredForEngine)
        {
            extraSourceWarnings.addIfNotAlreadyThere("SOURCE_MISSING");
            didaPresetManagerLog("diapreset source folder missing in category=" + effectiveCategory
                + " presetCategoryFolder=" + presetCategoryFolder.getFullPathName()
                + " expectedSourceFolderName=" + expectedSourceFolderName
                + " path=" + up.source.path);
        }
        else if (! resolved.isDirectory())
        {
            didaPresetManagerLog("diapreset has no source folder but engine does not require one"
                " (engineType=" + up.engineType + ") name=" + up.presetName);
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
        requestedSampleRawPath     = rawSourcePath;
        requestedSampleResolvedFrom = resolvedFrom;
        requestedSampleRawInsidePresetsUser = rawIsInsidePresetsUser;
        requestedPresetFilePath          = file.getFullPathName();
        requestedPresetCategoryFolder    = presetCategoryFolder.getFullPathName();
        requestedExpectedSourceFolderName = expectedSourceFolderName;
        requestedAllowCrossCategorySource = allowCrossCategorySource;
        requestedSourceFolderWavCount    = 0;
        requestedAiTextureBaseSourceRaw = aiTextureBaseSourceRaw;
        requestedAiTextureBaseSourceResolvedCandidate = aiTextureBaseSourceResolvedCandidate;
        requestedAiTextureBaseSourceExists = aiTextureBaseSourceExists;
        requestedAiTextureBaseSourceWavCount = aiTextureBaseSourceWavCount;
        requestedExtraSourceWarnings     = extraSourceWarnings;
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
            requestedSourceFolderWavCount = files.size();


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
    {
        // Report 78: master gain is a SMALL final trim only. Legacy presets
        // stored huge loudness corrections here (-60/+12) — clamp them into the
        // safe ±6 dB range so they can never collapse or overboost the dry bus,
        // and record the raw requested value so the reporter can flag the clamp.
        const double rawMaster = json.hasProperty(key::masterGain)
                                   ? (double) json.getProperty(key::masterGain, 0.0) : 0.0;
        if (auto* dp = dynamic_cast<DiditagainProcessor*>(&processor))
            dp->getSynthEngine().getFx().noteRequestedMasterGainDb((float) rawMaster);
        setParam(processor, "masterGain", juce::jlimit(-6.0, 6.0, rawMaster));
    }
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

//==============================================================================
//  AI Texture Import + Freeze v0.2 — message-thread only. No realtime neural
//  inference: we only validate, copy and reference a cached WAV texture.
//==============================================================================
static int aiFindNeuralPartialIndex(const dida::userpreset::UserPreset& p)
{
    for (int i = 0; i < p.partials.size(); ++i)
    {
        const auto& pb = p.partials.getReference(i);
        if (pb.engineType.equalsIgnoreCase("neuralTextureCached")
            || pb.engineType.equalsIgnoreCase("neuralTexture"))
            return i;
    }
    return -1;
}

juce::File PresetManager::aiNeuralTextureFolder() const
{
    auto docsRoot = dida::SampleLibrary::getSamplesRoot().getParentDirectory();
    const auto cat = juce::File::createLegalFileName(
        pendingUserDiapreset.category.isNotEmpty() ? pendingUserDiapreset.category
                                                   : juce::String("Uncategorized"));
    const auto name = juce::File::createLegalFileName(
        pendingUserDiapreset.presetName.isNotEmpty() ? pendingUserDiapreset.presetName
                                                     : juce::String("Preset"));
    return docsRoot.getChildFile("NeuralTextures").getChildFile(cat).getChildFile(name);
}

juce::String PresetManager::aiTextureStatus() const
{
    if (! aiHasEditablePreset()) return "Disabled";
    const int idx = aiFindNeuralPartialIndex(pendingUserDiapreset);
    if (idx < 0) return "Disabled";
    const auto& pb = pendingUserDiapreset.partials.getReference(idx);
    const juce::String texPath = pb.engineParams.getProperty("texturePath", "").toString();
    if (texPath.isEmpty()) return "Missing";
    const juce::File f = dida::userpreset::resolveSourcePath(texPath);
    if (! f.existsAsFile()) return "Missing";
    const auto norm = f.getFullPathName().replaceCharacter('\\', '/');
    const bool managed = norm.containsIgnoreCase("/NeuralTextures/");
    if (texPath.startsWithIgnoreCase("{DIDA_DOCS}") && managed) return "Cached";
    if (managed) return "Imported";
    return "External";
}

// Re-apply the on-disk preset after we mutated + wrote it, so the audio engine
// picks up the new partial set. Uses the public diapreset load path by index.
static void aiReloadByPath(PresetManager& pm, const juce::String& path)
{
    pm.scanPresetDirectory();
    for (int i = 0; i < pm.getNumPresets(); ++i)
        if (pm.getPresetUserFile(i) == path || pm.getPresetFilePath(i) == path)
        { pm.loadPreset(i); return; }
}

PresetManager::AiTextureOpResult PresetManager::aiImportTextureWav(const juce::File& wav)
{
    AiTextureOpResult r;
    if (! aiHasEditablePreset())
    { r.status = "Disabled"; r.message = "No editable preset loaded."; return r; }

    if (! wav.existsAsFile())
    { r.status = aiTextureStatus(); r.message = "File not found."; return r; }

    // Validate the audio is readable.
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(wav));
    if (reader == nullptr || reader->lengthInSamples <= 0)
    { r.status = aiTextureStatus(); r.message = "Unreadable / invalid audio."; return r; }
    reader.reset();

    // Copy into the managed folder.
    auto folder = aiNeuralTextureFolder();
    if (! folder.createDirectory())
    { r.status = aiTextureStatus(); r.message = "Could not create texture folder."; return r; }

    auto safeName = juce::File::createLegalFileName(wav.getFileName());
    auto dest = folder.getChildFile(safeName);
    if (dest.existsAsFile()) dest = folder.getNonexistentChildFile(
        wav.getFileNameWithoutExtension(), "." + wav.getFileExtension().trimCharactersAtStart("."), false);
    if (! wav.copyFileTo(dest))
    { r.status = aiTextureStatus(); r.message = "Copy into managed folder failed."; return r; }

    // Build a portable {DIDA_DOCS} token path.
    auto docsRoot = dida::SampleLibrary::getSamplesRoot().getParentDirectory();
    juce::String portable = "{DIDA_DOCS}/" + dest.getRelativePathFrom(docsRoot)
                                                 .replaceCharacter('\\', '/');

    // Attach / update the neuralTextureCached partial.
    auto& up = pendingUserDiapreset;
    int idx = aiFindNeuralPartialIndex(up);
    if (idx < 0)
    {
        if (up.partials.size() >= 4)
        { r.status = aiTextureStatus(); r.message = "Preset already has 4 partials."; return r; }
        dida::userpreset::UserPreset::PartialBlock pb;
        up.partials.add(pb);
        idx = up.partials.size() - 1;
    }
    auto& pb = up.partials.getReference(idx);
    pb.enabled            = true;
    pb.engineType         = "neuralTextureCached";
    pb.eqRole             = "neuralTexture";
    pb.followMainEnvelope = true;
    if (pb.amp.releaseMs <= 0.0f) pb.amp.releaseMs = 250.0f;

    auto* ep = new juce::DynamicObject();
    ep->setProperty("texturePath",   portable);
    ep->setProperty("loop",          true);
    ep->setProperty("rootMidi",      60);
    ep->setProperty("pitchTracking", true);
    ep->setProperty("levelDb",       -18.0);
    pb.engineParams = juce::var(ep);

    up.ai.present     = true;
    up.ai.enabled     = true;
    up.ai.textureMode = "cached";

    // Persist + reload so the engine instantiates the new texture partial.
    juce::File presetFile(requestedPresetFilePath);
    presetFile.replaceWithText(dida::userpreset::toJson(up));
    aiReloadByPath(*this, requestedPresetFilePath);

    r.ok = true; r.status = "Imported";
    r.message = "Imported texture: " + dest.getFileName();
    return r;
}

PresetManager::AiTextureOpResult PresetManager::aiRemoveTexture()
{
    AiTextureOpResult r;
    if (! aiHasEditablePreset())
    { r.status = "Disabled"; r.message = "No editable preset loaded."; return r; }

    auto& up = pendingUserDiapreset;
    const int idx = aiFindNeuralPartialIndex(up);
    if (idx < 0)
    { r.status = "Disabled"; r.message = "No texture attached."; return r; }

    up.partials.remove(idx);            // does NOT delete the WAV on disk
    up.ai.enabled = false;

    juce::File presetFile(requestedPresetFilePath);
    presetFile.replaceWithText(dida::userpreset::toJson(up));
    aiReloadByPath(*this, requestedPresetFilePath);

    r.ok = true; r.status = "Disabled";
    r.message = "Texture removed (WAV kept on disk).";
    return r;
}

PresetManager::AiTextureOpResult PresetManager::aiFreezeTexture()
{
    AiTextureOpResult r;
    if (! aiHasEditablePreset())
    { r.status = "Disabled"; r.message = "No editable preset loaded."; return r; }

    auto& up = pendingUserDiapreset;
    const int idx = aiFindNeuralPartialIndex(up);
    if (idx < 0)
    { r.status = "Disabled"; r.message = "FREEZE_MISSING_SOURCE: no texture attached."; return r; }

    auto& pb = up.partials.getReference(idx);
    const juce::String texPath = pb.engineParams.getProperty("texturePath", "").toString();
    juce::File srcFile = texPath.isNotEmpty() ? dida::userpreset::resolveSourcePath(texPath)
                                              : juce::File();
    if (texPath.isEmpty() || ! srcFile.existsAsFile())
    { r.status = "Missing"; r.message = "FREEZE_MISSING_SOURCE: texture file missing."; return r; }

    // Ensure the texture lives inside the managed folder; copy it in if external.
    auto folder = aiNeuralTextureFolder();
    const auto norm = srcFile.getFullPathName().replaceCharacter('\\', '/');
    juce::String portable = texPath;
    if (! norm.containsIgnoreCase("/NeuralTextures/"))
    {
        folder.createDirectory();
        auto dest = folder.getChildFile(juce::File::createLegalFileName(srcFile.getFileName()));
        if (dest != srcFile && ! dest.existsAsFile()) srcFile.copyFileTo(dest);
        auto docsRoot = dida::SampleLibrary::getSamplesRoot().getParentDirectory();
        portable = "{DIDA_DOCS}/" + dest.getRelativePathFrom(docsRoot).replaceCharacter('\\', '/');
    }

    if (auto* ep = pb.engineParams.getDynamicObject())
        ep->setProperty("texturePath", portable);
    pb.enabled = true;
    pb.engineType = "neuralTextureCached";
    up.ai.present     = true;
    up.ai.enabled     = true;
    up.ai.textureMode = "cached";

    juce::File presetFile(requestedPresetFilePath);
    presetFile.replaceWithText(dida::userpreset::toJson(up));
    aiReloadByPath(*this, requestedPresetFilePath);

    r.ok = true; r.status = "Cached";
    r.message = "FREEZE_READY: texture frozen to preset.";
    return r;
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
// Write an embedded BinaryData resource (looked up by its ORIGINAL filename,
// e.g. "dida_brass_air_C4.wav") to disk, creating parent folders. Never
// overwrites an existing file so the user's edits/imports are preserved.
// Returns true when the destination exists after the call.
//==============================================================================
static bool writeBinaryResourceToFile(const juce::String& originalName,
                                      const juce::File& dest)
{
    if (dest.existsAsFile())
        return true;

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (juce::String(BinaryData::originalFilenames[i]) != originalName)
            continue;

        int size = 0;
        if (const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size))
        {
            dest.getParentDirectory().createDirectory();
            juce::FileOutputStream os(dest);
            if (os.openedOk())
            {
                os.write(data, (size_t) size);
                return true;
            }
        }
        return false;
    }
    return false;
}

static juce::String aiTextureDemoSourcePathForFile(const juce::String& fileName)
{
    const auto n = fileName.toLowerCase();
    if (n.contains("brass air"))
        return "{DIDA_DOCS}/Samples/Presets/User/Brass/Brass 1";
    if (n.contains("choir ghost"))
        return "{DIDA_DOCS}/Samples/Presets/User/Choir Ohhh";
    if (n.contains("guitar dust"))
        return "{DIDA_DOCS}/Samples/Presets/User/Acoustic Guitars/Acoustic Guitar 1";
    return {};
}

static void forceAiTextureDemoSourcePath(const juce::File& presetFile)
{
    const auto targetPath = aiTextureDemoSourcePathForFile(presetFile.getFileName());
    if (targetPath.isEmpty() || ! presetFile.existsAsFile()) return;

    auto json = juce::JSON::parse(presetFile);
    auto* obj = json.getDynamicObject();
    if (obj == nullptr) return;

    auto src = obj->getProperty("sourceInstrument");
    juce::DynamicObject::Ptr newSrc;
    auto* srcObj = src.getDynamicObject();
    if (srcObj == nullptr)
    {
        newSrc = new juce::DynamicObject();
        srcObj = newSrc.get();
    }
    srcObj->setProperty("type", "multisampleFolder");
    srcObj->setProperty("path", targetPath);
    srcObj->setProperty("mappingMode", "nearest");
    juce::Array<juce::var> roots;
    roots.add("C");
    srcObj->setProperty("rootNotePattern", roots);
    if (newSrc != nullptr)
        obj->setProperty("sourceInstrument", juce::var(newSrc.get()));

    if (auto* samplesObj = obj->getProperty("samples").getDynamicObject())
        samplesObj->setProperty("rootFolder", targetPath);

    presetFile.replaceWithText(juce::JSON::toString(json, false));
}

//==============================================================================
// One-time install of the bundled "AI Texture Demo Pack".
//
// Lays the pack out exactly where the engine expects to find it:
//   <Samples>/Presets/User/AI Texture/*.diapreset        (browser category)
//   <Docs>/NeuralTextures/Demo/<Type>/*.wav              ({DIDA_DOCS} target)
//
// The presets reference their textures with the portable {DIDA_DOCS} token, so
// once the WAVs land under <Docs>/NeuralTextures/Demo/ they resolve cleanly on
// any machine. Behind a .seeded marker so user deletions stay deleted.
//==============================================================================
void PresetManager::seedAiTextureDemoPackIfMissing()
{
    auto root = getUserPresetDirectory();
    auto dir  = root.getChildFile("AI Texture");
    dir.createDirectory();

    // v11 migration marker. v6 fixed the multisample source paths; v7 recalibrated
    // loudness; v8 re-balanced the output; v9 set AI Guitar Dust Test body gain to
    // 0 dB. v10 re-tuned loudness (Brass +3 -> +2 dB, Guitar 0 -> +7 dB). v11 sets
    // AI Guitar Dust Test ampEnvelope gain +7 -> +2 dB. Re-run once to overwrite
    // the managed demo .diapreset files.
    auto seededMarkerV11 = dir.getChildFile(".seeded_ai_texture_demo_v11");
    if (seededMarkerV11.existsAsFile())
        return;


    auto docsRoot = dida::SampleLibrary::getSamplesRoot().getParentDirectory();
    auto texRoot  = docsRoot.getChildFile("NeuralTextures").getChildFile("Demo");

    int written = 0;

    // 1) Texture WAVs into the managed {DIDA_DOCS}/NeuralTextures/Demo tree.
    //    These paths match the presets' {DIDA_DOCS}/NeuralTextures/Demo/<Type>/
    //    texturePath tokens exactly, so they resolve on every machine.
    written += writeBinaryResourceToFile("dida_brass_air_C4.wav",
                   texRoot.getChildFile("BrassAir").getChildFile("dida_brass_air_C4.wav")) ? 1 : 0;
    written += writeBinaryResourceToFile("dida_choir_ghost_C4.wav",
                   texRoot.getChildFile("ChoirGhost").getChildFile("dida_choir_ghost_C4.wav")) ? 1 : 0;
    written += writeBinaryResourceToFile("dida_guitar_dust_C4.wav",
                   texRoot.getChildFile("GuitarDust").getChildFile("dida_guitar_dust_C4.wav")) ? 1 : 0;

    // 2) Preset JSON into the browser-visible "AI Texture" category folder.
    const char* presetFiles[] = {
        "AI Brass Air Test.diapreset",
        "AI Choir Ghost Test.diapreset",
        "AI Guitar Dust Test.diapreset"
    };
    // v7: refresh the three managed AI Texture demo presets in place so installed
    // copies get the recalibrated gains (and exact sourceInstrument.path values).
    // This does not touch any other presets or non-managed user content.
    for (auto* name : presetFiles)
    {
        auto destFile = dir.getChildFile(name);
        if (destFile.existsAsFile())
            destFile.deleteFile();
        const bool installed = writeBinaryResourceToFile(name, destFile);
        if (installed)
        {
            forceAiTextureDemoSourcePath(destFile);
            ++written;
        }
    }

    seededMarkerV11.replaceWithText("1");

    didaPresetManagerLog("seeded AI Texture demo pack (v8) count=" + juce::String(written)
        + " presets=" + dir.getFullPathName()
        + " textures=" + texRoot.getFullPathName());
}

//==============================================================================
// installPresetPackFromZip
//
// Message-thread only. Extracts a DIDITAGAIN preset/audio pack ZIP and copies
// its contents into the managed Documents tree so nothing ever references the
// temporary extraction path:
//   *.diapreset            -> <Samples>/Presets/User/AI Texture/
//   NeuralTextures/**/*.wav -> <Docs>/NeuralTextures/**     (preserved subtree)
//   <other>/*.wav under a NeuralTextures-less pack -> <Docs>/NeuralTextures/Imported/
//
// Presets keep their {DIDA_DOCS} texture tokens, so after install they resolve
// against the managed folder, not the ZIP. Returns a short status/message.
//==============================================================================
PresetManager::AiTextureOpResult PresetManager::installPresetPackFromZip(const juce::File& zip)
{
    AiTextureOpResult r;

    if (! zip.existsAsFile())
    { r.status = "Missing ZIP"; r.message = "Pack file not found."; return r; }

    juce::ZipFile archive(zip);
    if (archive.getNumEntries() == 0)
    { r.status = "Empty/invalid"; r.message = "Could not read the pack ZIP."; return r; }

    auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("DIDA_PackImport_" + juce::String(juce::Time::getMillisecondCounter()));
    temp.createDirectory();
    auto cleanup = [&temp] { temp.deleteRecursively(); };

    if (! archive.uncompressTo(temp, true).wasOk())
    { cleanup(); r.status = "Extract failed"; r.message = "Could not extract the pack ZIP."; return r; }

    auto docsRoot   = dida::SampleLibrary::getSamplesRoot().getParentDirectory();
    auto presetDir  = getUserPresetDirectory().getChildFile("AI Texture");
    auto texRoot    = docsRoot.getChildFile("NeuralTextures");
    presetDir.createDirectory();
    texRoot.createDirectory();

    int presetsCopied = 0, texturesCopied = 0;

    // Copy presets.
    for (auto& f : temp.findChildFiles(juce::File::findFiles, true, "*.diapreset"))
        if (f.copyFileTo(presetDir.getChildFile(f.getFileName())))
            ++presetsCopied;

    // Copy textures, preserving any "NeuralTextures/..." subtree from the pack
    // so {DIDA_DOCS}/NeuralTextures/<...> tokens resolve. WAVs outside such a
    // subtree go under NeuralTextures/Imported/.
    for (auto& f : temp.findChildFiles(juce::File::findFiles, true, "*.wav"))
    {
        const auto norm = f.getFullPathName().replaceCharacter('\\', '/');
        const int marker = norm.indexOfIgnoreCase("/NeuralTextures/");
        juce::File dest;
        if (marker >= 0)
            dest = texRoot.getChildFile(norm.substring(marker + (int) juce::String("/NeuralTextures/").length()));
        else
            dest = texRoot.getChildFile("Imported").getChildFile(f.getFileName());

        dest.getParentDirectory().createDirectory();
        if (f.copyFileTo(dest))
            ++texturesCopied;
    }

    cleanup();

    if (presetsCopied == 0 && texturesCopied == 0)
    { r.status = "Nothing installed"; r.message = "No presets or textures found in the ZIP."; return r; }

    scanPresetDirectory();
    if (onPresetLoaded) onPresetLoaded();

    r.ok = true;
    r.status = "Installed";
    r.message = "Installed " + juce::String(presetsCopied) + " preset(s) and "
              + juce::String(texturesCopied) + " texture(s).";
    didaPresetManagerLog("installPresetPackFromZip " + r.message + " from " + zip.getFullPathName());
    return r;
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
    // Disabled by design: subfolders under <Samples>/Presets/User/<Category>/
    // are hidden source folders consumed by .diapreset routing only. They
    // must NOT appear as their own browser entries.
    if (! showSampleFoldersInBrowser) return;

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

