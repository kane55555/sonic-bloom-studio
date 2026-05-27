#pragma once
//==============================================================================
//  SourceFolderValidator.h — DIDITAGAIN STUDIO hidden source-folder validator.
//
//  Walks every category under:
//      <UserDocs>/DIDITAGAIN STUDIO/Samples/Presets/User/<Category>/
//  and inspects each hidden source sub-folder (sibling of .diapreset files).
//
//  Reports per-folder:
//    - category, source folder name + full path
//    - WAV count, detected notes, missing/duplicate notes
//    - invalid note names, bad filename formats
//    - lowest/highest note, whether multisample zones can be built
//    - recommended mapping mode (chromatic / sparse / single)
//
//  Writes:
//    <Logs>/source_folder_validation.json
//    <Logs>/source_folder_validation.txt
//  Plus a "[DIDITAGAIN source-validator]" line per folder to the JUCE log.
//
//  Read-only. Does NOT touch audio behaviour or preset routing.
//==============================================================================
#include <JuceHeader.h>
#include "PresetQualityReport.h"   // for logsDir()

namespace dida { namespace sourcevalidator {

struct NoteInfo
{
    int  midi  = -1;          // 0..127, -1 if invalid
    int  count = 0;           // how many files use this note
    juce::String label;       // canonical "C#3" etc.
};

struct FolderReport
{
    juce::String category;
    juce::String folderName;
    juce::String folderPath;
    int wavCount = 0;
    juce::StringArray detectedNotes;       // canonical labels
    juce::StringArray missingExpected;     // expected but missing
    juce::StringArray duplicateNotes;      // notes with >1 file
    juce::StringArray invalidNoteFiles;    // filename -> bad note token
    juce::StringArray badFormatFiles;      // filename has no parseable note
    bool canBuildZones = false;
    int  lowestMidi = -1;
    int  highestMidi = -1;
    juce::String lowestNote;
    juce::String highestNote;
    juce::String recommendedMapping;       // "chromatic" / "sparse" / "single" / "none"
    juce::StringArray warnings;
};

inline juce::File samplesRoot()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("DIDITAGAIN STUDIO").getChildFile("Samples");
}

inline juce::String midiToLabel(int midi)
{
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    if (midi < 0 || midi > 127) return {};
    const int oct = (midi / 12) - 1;
    return juce::String(names[midi % 12]) + juce::String(oct);
}

// Parse a single note token like "C3", "F#4", "Db2", "Bb-1", "Fs4".
// Returns -1 if not a valid note token.
inline int parseNoteToken(const juce::String& tokIn)
{
    auto tok = tokIn.trim();
    if (tok.isEmpty()) return -1;

    const juce::juce_wchar L = juce::CharacterFunctions::toUpperCase(tok[0]);
    if (L < 'A' || L > 'G') return -1;
    static const int semi[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A..G
    int pc = semi[L - 'A'];

    int pos = 1;
    if (pos < tok.length())
    {
        const auto c = tok[pos];
        if (c == '#' || c == 's' || c == 'S') { pc += 1; ++pos; }
        else if (c == 'b' && pos + 1 < tok.length()
                 && (juce::CharacterFunctions::isDigit(tok[pos + 1]) || tok[pos + 1] == '-'))
        { pc -= 1; ++pos; }
    }
    if (pos >= tok.length()) return -1;
    auto octStr = tok.substring(pos);
    if (! octStr.containsOnly("-0123456789")) return -1;
    const int oct = octStr.getIntValue();
    const int midi = (oct + 1) * 12 + pc;
    if (midi < 0 || midi > 127) return -1;
    return midi;
}

// Pull the trailing note token from a filename stem.
// Returns the parsed midi (or -1) and the raw token actually inspected.
inline int parseTrailingNote(const juce::String& stem, juce::String& tokenOut)
{
    int sep = -1;
    for (int i = stem.length() - 1; i >= 0; --i)
    {
        const auto c = stem[i];
        if (c == '_' || c == ' ' || c == '-') { sep = i; break; }
    }
    tokenOut = (sep >= 0 ? stem.substring(sep + 1) : stem);
    return parseNoteToken(tokenOut);
}

inline void validateFolder(const juce::String& category,
                           const juce::File& folder,
                           FolderReport& r)
{
    r.category   = category;
    r.folderName = folder.getFileName();
    r.folderPath = folder.getFullPathName();

    if (! folder.isDirectory())
    {
        r.warnings.add("SOURCE_FOLDER_EMPTY");
        r.recommendedMapping = "none";
        return;
    }

    auto wavs = folder.findChildFiles(juce::File::findFiles, false, "*.wav");
    r.wavCount = wavs.size();
    if (wavs.isEmpty())
    {
        r.warnings.add("SOURCE_FOLDER_EMPTY");
        r.recommendedMapping = "none";
        return;
    }

    std::map<int, int> midiCounts;
    int validCount = 0;

    for (auto& f : wavs)
    {
        const auto stem = f.getFileNameWithoutExtension();
        juce::String tok;
        const int midi = parseTrailingNote(stem, tok);

        if (midi < 0)
        {
            // Distinguish: no token vs. bad note token.
            if (tok.isEmpty() || ! stem.containsAnyOf("_- "))
                r.badFormatFiles.add(f.getFileName());
            else
                r.invalidNoteFiles.add(f.getFileName() + " [token=" + tok + "]");
            continue;
        }

        ++validCount;
        midiCounts[midi]++;
    }

    if (validCount == 0)
    {
        r.warnings.add("NO_VALID_WAVS");
        if (! r.invalidNoteFiles.isEmpty()) r.warnings.add("INVALID_NOTE_NAME");
        if (! r.badFormatFiles.isEmpty())   r.warnings.add("BAD_FILENAME_FORMAT");
        r.recommendedMapping = "none";
        return;
    }

    if (! r.invalidNoteFiles.isEmpty()) r.warnings.add("INVALID_NOTE_NAME");
    if (! r.badFormatFiles.isEmpty())   r.warnings.add("BAD_FILENAME_FORMAT");

    for (auto& kv : midiCounts)
    {
        const auto lbl = midiToLabel(kv.first);
        r.detectedNotes.add(lbl);
        if (kv.second > 1)
        {
            r.duplicateNotes.add(lbl + " x" + juce::String(kv.second));
        }
    }
    if (! r.duplicateNotes.isEmpty()) r.warnings.add("DUPLICATE_NOTE");

    r.lowestMidi  = midiCounts.begin()->first;
    r.highestMidi = midiCounts.rbegin()->first;
    r.lowestNote  = midiToLabel(r.lowestMidi);
    r.highestNote = midiToLabel(r.highestMidi);

    const int uniqueNotes = (int) midiCounts.size();
    const int span = r.highestMidi - r.lowestMidi + 1;

    // Determine mapping mode + expected template.
    // Sparse: notes are a subset of {C,D#,F#,A} per octave AND uniqueNotes < span/2.
    auto pcOf = [](int m) { return ((m % 12) + 12) % 12; };
    bool allSparsePc = true;
    for (auto& kv : midiCounts)
    {
        const int pc = pcOf(kv.first);
        if (pc != 0 && pc != 3 && pc != 6 && pc != 9) { allSparsePc = false; break; }
    }

    if (uniqueNotes == 1)
    {
        r.recommendedMapping = "single";
    }
    else if (allSparsePc && uniqueNotes <= (span / 3 + 4))
    {
        r.recommendedMapping = "sparse";
        // Expect C, D#, F#, A in every octave covered.
        const int loOct = (r.lowestMidi / 12) - 1;
        const int hiOct = (r.highestMidi / 12) - 1;
        for (int o = loOct; o <= hiOct; ++o)
            for (int pc : { 0, 3, 6, 9 })
            {
                const int midi = (o + 1) * 12 + pc;
                if (midi < r.lowestMidi || midi > r.highestMidi) continue;
                if (midiCounts.find(midi) == midiCounts.end())
                    r.missingExpected.add(midiToLabel(midi));
            }
    }
    else
    {
        r.recommendedMapping = "chromatic";
        for (int m = r.lowestMidi; m <= r.highestMidi; ++m)
            if (midiCounts.find(m) == midiCounts.end())
                r.missingExpected.add(midiToLabel(m));
    }

    if (! r.missingExpected.isEmpty()) r.warnings.add("MISSING_EXPECTED_NOTE");

    // Zones can be built if we have at least one valid, mapped note.
    r.canBuildZones = (uniqueNotes >= 1);
    if (! r.canBuildZones) r.warnings.add("ZONE_BUILD_FAILED");
}

inline juce::String quote(const juce::String& s)
{
    return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
}

inline juce::String jsonArray(const juce::StringArray& a)
{
    juce::String out = "[";
    for (int i = 0; i < a.size(); ++i)
    {
        if (i) out << ",";
        out << quote(a[i]);
    }
    out << "]";
    return out;
}

inline juce::String folderToJson(const FolderReport& r)
{
    juce::String j;
    j << "{"
      << "\"category\":"          << quote(r.category) << ","
      << "\"sourceFolderName\":"  << quote(r.folderName) << ","
      << "\"sourceFolderPath\":"  << quote(r.folderPath) << ","
      << "\"wavCount\":"          << r.wavCount << ","
      << "\"detectedNotes\":"     << jsonArray(r.detectedNotes) << ","
      << "\"missingExpected\":"   << jsonArray(r.missingExpected) << ","
      << "\"duplicateNotes\":"    << jsonArray(r.duplicateNotes) << ","
      << "\"invalidNoteFiles\":"  << jsonArray(r.invalidNoteFiles) << ","
      << "\"badFormatFiles\":"    << jsonArray(r.badFormatFiles) << ","
      << "\"canBuildZones\":"     << (r.canBuildZones ? "true" : "false") << ","
      << "\"lowestMidi\":"        << r.lowestMidi << ","
      << "\"highestMidi\":"       << r.highestMidi << ","
      << "\"lowestNote\":"        << quote(r.lowestNote) << ","
      << "\"highestNote\":"       << quote(r.highestNote) << ","
      << "\"recommendedMapping\":" << quote(r.recommendedMapping) << ","
      << "\"warnings\":"          << jsonArray(r.warnings)
      << "}";
    return j;
}

inline juce::String folderToText(const FolderReport& r)
{
    juce::String t;
    t << "------------------------------------------------------------\n"
      << "category            : " << r.category << "\n"
      << "sourceFolderName    : " << r.folderName << "\n"
      << "sourceFolderPath    : " << r.folderPath << "\n"
      << "wavCount            : " << r.wavCount << "\n"
      << "detectedNotes       : " << r.detectedNotes.joinIntoString(", ") << "\n"
      << "missingExpected     : " << r.missingExpected.joinIntoString(", ") << "\n"
      << "duplicateNotes      : " << r.duplicateNotes.joinIntoString(", ") << "\n"
      << "invalidNoteFiles    : " << r.invalidNoteFiles.joinIntoString(", ") << "\n"
      << "badFormatFiles      : " << r.badFormatFiles.joinIntoString(", ") << "\n"
      << "canBuildZones       : " << (r.canBuildZones ? "true" : "false") << "\n"
      << "lowestNote          : " << r.lowestNote << " (" << r.lowestMidi << ")\n"
      << "highestNote         : " << r.highestNote << " (" << r.highestMidi << ")\n"
      << "recommendedMapping  : " << r.recommendedMapping << "\n"
      << "warnings            : " << (r.warnings.isEmpty() ? juce::String("none")
                                                          : r.warnings.joinIntoString(", ")) << "\n";
    return t;
}

inline void logFolder(const FolderReport& r)
{
    juce::String line;
    line << "[DIDITAGAIN source-validator]"
         << " category=" << r.category
         << " folder=" << r.folderName
         << " path=" << r.folderPath
         << " wavCount=" << r.wavCount
         << " unique=" << r.detectedNotes.size()
         << " range=" << r.lowestNote << ".." << r.highestNote
         << " mapping=" << r.recommendedMapping
         << " canBuildZones=" << (r.canBuildZones ? "true" : "false")
         << " missing=" << r.missingExpected.size()
         << " duplicates=" << r.duplicateNotes.size()
         << " invalid=" << r.invalidNoteFiles.size()
         << " badFormat=" << r.badFormatFiles.size()
         << " warnings=" << (r.warnings.isEmpty() ? juce::String("none")
                                                  : r.warnings.joinIntoString("|"));
    juce::Logger::writeToLog(line);
}

// Scan every category under Samples/Presets/User and report on every hidden
// source sub-folder. Safe to call from PresetManager::scanPresetDirectory().
inline std::vector<FolderReport> validateAll()
{
    std::vector<FolderReport> reports;
    auto presetsUser = samplesRoot().getChildFile("Presets").getChildFile("User");
    if (! presetsUser.isDirectory())
    {
        juce::Logger::writeToLog("[DIDITAGAIN source-validator] presetsUser missing: "
                                 + presetsUser.getFullPathName());
        return reports;
    }

    auto categories = presetsUser.findChildFiles(juce::File::findDirectories, false);
    for (auto& cat : categories)
    {
        const auto catName = cat.getFileName();
        auto subs = cat.findChildFiles(juce::File::findDirectories, false);
        for (auto& sub : subs)
        {
            FolderReport r;
            validateFolder(catName, sub, r);
            logFolder(r);
            reports.push_back(std::move(r));
        }
    }

    // Write JSON + TXT reports.
    auto dir = dida::presetreport::logsDir();
    auto jsonFile = dir.getChildFile("source_folder_validation.json");
    auto txtFile  = dir.getChildFile("source_folder_validation.txt");

    juce::String json;
    json << "{\n  \"generatedAt\":" << quote(juce::Time::getCurrentTime().toISO8601(true))
         << ",\n  \"presetsUserRoot\":" << quote(presetsUser.getFullPathName())
         << ",\n  \"folderCount\":" << (int) reports.size()
         << ",\n  \"folders\":[\n";
    for (size_t i = 0; i < reports.size(); ++i)
    {
        json << "    " << folderToJson(reports[i]);
        if (i + 1 < reports.size()) json << ",";
        json << "\n";
    }
    json << "  ]\n}\n";
    jsonFile.replaceWithText(json);

    juce::String txt;
    txt << "DIDITAGAIN STUDIO — Source Folder Validation\n"
        << "generatedAt : " << juce::Time::getCurrentTime().toISO8601(true) << "\n"
        << "root        : " << presetsUser.getFullPathName() << "\n"
        << "folderCount : " << (int) reports.size() << "\n";
    for (auto& r : reports) txt << folderToText(r);
    txt << "------------------------------------------------------------\n";
    txtFile.replaceWithText(txt);

    juce::Logger::writeToLog("[DIDITAGAIN source-validator] wrote "
        + jsonFile.getFullPathName() + " and " + txtFile.getFullPathName()
        + " folders=" + juce::String((int) reports.size()));

    return reports;
}

}} // namespace dida::sourcevalidator
