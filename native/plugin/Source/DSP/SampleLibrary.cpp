#include "SampleLibrary.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>

namespace dida {

//==============================================================================
//  Filename parsing
//==============================================================================
static int noteNameToMidi(const juce::String& noteToken)
{
    // Examples: "C3", "F#3", "Bb4", "A-1"
    if (noteToken.isEmpty()) return -1;

    juce::String s = noteToken.trim();
    int pos = 0;
    const auto upperFirst = juce::CharacterFunctions::toUpperCase(s[0]);

    static const int letterToSemis[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    if (upperFirst < 'A' || upperFirst > 'G') return -1;
    int semis = letterToSemis[upperFirst - 'A'];
    ++pos;

    if (pos < s.length() && (s[pos] == '#' || s[pos] == 'b' || s[pos] == 'B'))
    {
        if (s[pos] == '#')      semis += 1;
        else if (s[pos] == 'b') semis -= 1;
        // capital B alone is the note "B" handled above; only flats use lower b.
        ++pos;
    }

    if (pos >= s.length()) return -1;

    auto octStr = s.substring(pos);
    if (! octStr.containsOnly("-0123456789")) return -1;
    const int octave = octStr.getIntValue();

    // C4 = MIDI 60  =>  midi = (octave + 1) * 12 + semis
    const int midi = (octave + 1) * 12 + semis;
    if (midi < 0 || midi > 127) return -1;
    return midi;
}

static juce::String midiToNoteName(int midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    midi = juce::jlimit(0, 127, midi);
    return juce::String(names[midi % 12]) + juce::String((midi / 12) - 1);
}

// Parse "Brass_C3" or "Brass_F#3_v90" out of a filename stem.
// Returns true on success and fills root + hiVel (hiVel = 127 if not present).
static bool parseSampleName(const juce::String& stem, int& rootMidi, int& hiVel)
{
    rootMidi = -1;
    hiVel = 127;

    auto tokens = juce::StringArray::fromTokens(stem, "_-. ", "");
    if (tokens.isEmpty()) return false;

    // Walk tokens right-to-left; first try to read an optional vNN, then the note.
    int idx = tokens.size() - 1;

    // Optional velocity token
    auto& last = tokens.getReference(idx);
    if (last.length() >= 2 && (last[0] == 'v' || last[0] == 'V')
        && last.substring(1).containsOnly("0123456789"))
    {
        hiVel = juce::jlimit(1, 127, last.substring(1).getIntValue());
        --idx;
        if (idx < 0) return false;
    }

    // Note token
    rootMidi = noteNameToMidi(tokens[idx]);
    return rootMidi >= 0;
}

//==============================================================================
//  Multisample::pickZonesForNote
//==============================================================================
void Multisample::pickZonesForNote(int midi, int velocity,
                                   const SampleZone** lower,
                                   const SampleZone** upper,
                                   float& xfade) const noexcept
{
    *lower = nullptr;
    *upper = nullptr;
    xfade = 0.0f;
    if (zones.empty()) return;

    auto velMatches = [velocity](const SampleZone& z)
    {
        return velocity >= z.loVel && velocity <= z.hiVel;
    };

    for (auto& z : zones)
    {
        if (velMatches(z) && midi >= z.lowKey && midi <= z.highKey)
        {
            *lower = &z;
            return;
        }
    }

    // Velocity fallback: keep the hard key zone, but ignore velocity layers.
    for (auto& z : zones)
    {
        if (midi >= z.lowKey && midi <= z.highKey)
        {
            *lower = &z;
            return;
        }
    }

    const SampleZone* nearest = nullptr;
    int bestDist = std::numeric_limits<int>::max();
    for (auto& z : zones)
    {
        if (! velMatches(z)) continue;
        const int d = std::abs(midi - z.rootMidi);
        if (d < bestDist)
        {
            bestDist = d;
            nearest = &z;
        }
    }

    if (nearest == nullptr)
    {
        for (auto& z : zones)
        {
            const int d = std::abs(midi - z.rootMidi);
            if (d < bestDist) { bestDist = d; nearest = &z; }
        }
    }
    *lower = nearest;
}

//==============================================================================
//  SampleLibrary
//==============================================================================
juce::File SampleLibrary::getSamplesRoot()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("DIDITAGAIN STUDIO")
        .getChildFile("Samples");
}

juce::StringArray SampleLibrary::listInstruments()
{
    juce::StringArray out;
    auto root = getSamplesRoot();
    if (! root.isDirectory()) return out;

    for (auto& sub : root.findChildFiles(juce::File::findDirectories, false))
    {
        auto audio = sub.findChildFiles(juce::File::findFiles, false, "*.wav");
        if (! audio.isEmpty())
            out.add(sub.getFileName());
    }
    out.sort(true);
    return out;
}

namespace {
    std::mutex& cacheMutex()
    {
        static std::mutex m;
        return m;
    }
    std::unordered_map<std::string, std::shared_ptr<const Multisample>>& cache()
    {
        static std::unordered_map<std::string, std::shared_ptr<const Multisample>> c;
        return c;
    }

    juce::File resolveSampleSourceFile(const juce::String& sourcePath)
    {
        auto src = sourcePath.replace("\\", "/").trim();
        if (src.isEmpty()) return {};

        auto file = juce::File(src);
        if (juce::File::isAbsolutePath(src) && file.existsAsFile())
            return file;

        auto root = SampleLibrary::getSamplesRoot();
        if (src.startsWithIgnoreCase("Samples/"))
            file = root.getChildFile(src.substring(8));
        else
            file = root.getChildFile(src);

        if (! file.existsAsFile())
            juce::Logger::writeToLog("[DIDITAGAIN sample] missing source file: " + file.getFullPathName()
                + " from preset source=" + sourcePath);

        return file;
    }

    juce::AudioFormatManager& sharedFormatManager()
    {
        static juce::AudioFormatManager fm;
        static std::once_flag once;
        std::call_once(once, [] { fm.registerBasicFormats(); });
        return fm;
    }

    bool readSampleZoneFromFile(const juce::File& file, int rootMidi, int hiVel, SampleZone& zone)
    {
        if (! file.existsAsFile()) return false;

        auto& fm = sharedFormatManager();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
        if (reader == nullptr) return false;

        const int numSamples = static_cast<int>(reader->lengthInSamples);
        if (numSamples <= 0) return false;

        const int srcChannels = juce::jmax(1, (int) reader->numChannels);

        zone.sourceSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
        zone.rootMidi = juce::jlimit(0, 127, rootMidi);
        zone.loVel    = 0;
        zone.hiVel    = juce::jlimit(1, 127, hiVel);
        zone.fileName = file.getFileName();

        zone.buffer.setSize(2, numSamples);
        zone.buffer.clear();

        // Read into a temp buffer that matches the source channel count, then
        // splat to stereo. AudioFormatReader::read fills extra dest channels
        // by duplicating, which is what we want for mono -> stereo.
        if (srcChannels == 1)
        {
            juce::AudioBuffer<float> tmp(1, numSamples);
            if (! reader->read(&tmp, 0, numSamples, 0, true, false))
                return false;
            zone.buffer.copyFrom(0, 0, tmp, 0, 0, numSamples);
            zone.buffer.copyFrom(1, 0, tmp, 0, 0, numSamples);
        }
        else
        {
            if (! reader->read(&zone.buffer, 0, numSamples, 0, true, true))
                return false;
        }

        return true;
    }

    void assignStandardKeyZone(SampleZone& zone) noexcept
    {
        const int root = juce::jlimit(0, 127, zone.rootMidi);
        switch (root % 12)
        {
            case 0:  zone.lowKey = root;     zone.highKey = root + 1; break; // C -> C/C#
            case 3:  zone.lowKey = root - 1; zone.highKey = root + 1; break; // D# -> D/D#/E
            case 6:  zone.lowKey = root - 1; zone.highKey = root + 1; break; // F# -> F/F#/G
            case 9:  zone.lowKey = root - 1; zone.highKey = root + 2; break; // A -> G#/A/A#/B
            default: zone.lowKey = root;     zone.highKey = root;     break;
        }
        zone.lowKey = juce::jlimit(0, 127, zone.lowKey);
        zone.highKey = juce::jlimit(zone.lowKey, 127, zone.highKey);
    }

    std::shared_ptr<const Multisample> buildMultisampleFromFiles(const juce::Array<juce::File>& files,
                                                                 const juce::String& displayName)
    {
        auto ms = std::make_shared<Multisample>();
        ms->instrumentName = displayName.isNotEmpty() ? displayName : "Multisample";

        for (auto& file : files)
        {
            int rootMidi = 60;
            int hiVel = 127;
            if (! parseSampleName(file.getFileNameWithoutExtension(), rootMidi, hiVel))
            {
                juce::Logger::writeToLog("[DIDITAGAIN multisample] skipped unparseable file: " + file.getFileName());
                continue;
            }

            SampleZone zone;
            if (readSampleZoneFromFile(file, rootMidi, hiVel, zone))
            {
                assignStandardKeyZone(zone);
                ms->zones.push_back(std::move(zone));
            }
        }

        if (ms->zones.empty()) return nullptr;

        std::sort(ms->zones.begin(), ms->zones.end(),
            [](const SampleZone& a, const SampleZone& b)
            {
                if (a.rootMidi != b.rootMidi) return a.rootMidi < b.rootMidi;
                return a.hiVel < b.hiVel;
            });

        int i = 0;
        while (i < (int) ms->zones.size())
        {
            int j = i;
            while (j < (int) ms->zones.size() && ms->zones[j].rootMidi == ms->zones[i].rootMidi)
                ++j;
            int prevHi = -1;
            for (int k = i; k < j; ++k)
            {
                ms->zones[k].loVel = prevHi + 1;
                prevHi = ms->zones[k].hiVel;
            }
            ms->zones[j - 1].hiVel = 127;
            i = j;
        }

        juce::StringArray zoneDebug;
        for (const auto& z : ms->zones)
            zoneDebug.add(z.fileName + " root=" + midiToNoteName(z.rootMidi)
                + " zone=" + midiToNoteName(z.lowKey) + "-" + midiToNoteName(z.highKey));
        juce::Logger::writeToLog("[DIDITAGAIN multisample] loaded " + ms->instrumentName
            + " zones=" + juce::String((int) ms->zones.size())
            + " :: " + zoneDebug.joinIntoString(", "));

        return ms;
    }
}

void SampleLibrary::invalidateCache()
{
    std::lock_guard<std::mutex> lock(cacheMutex());
    cache().clear();
}

std::shared_ptr<const Multisample> SampleLibrary::loadInstrument(const juce::String& name)
{
    if (name.isEmpty()) return nullptr;

    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        auto it = cache().find(name.toStdString());
        if (it != cache().end()) return it->second;
    }

    auto folder = getSamplesRoot().getChildFile(name);
    if (! folder.isDirectory()) return nullptr;

    const auto wildcards = sharedFormatManager().getWildcardForAllFormats();
    auto files = folder.findChildFiles(juce::File::findFiles, false, wildcards);
    if (files.isEmpty()) return nullptr;

    auto ms = buildMultisampleFromFiles(files, name);
    if (ms == nullptr) return nullptr;

    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        cache()[name.toStdString()] = ms;
    }
    return ms;
}

std::shared_ptr<const Multisample> SampleLibrary::loadSampleSource(const juce::String& sourcePath,
                                                                   int rootMidi,
                                                                   const juce::String& displayName)
{
    auto file = resolveSampleSourceFile(sourcePath);
    if (! file.existsAsFile()) return nullptr;

    const auto cacheKey = (juce::String("sample:") + file.getFullPathName()
        + ":" + juce::String(rootMidi)).toStdString();

    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        auto it = cache().find(cacheKey);
        if (it != cache().end()) return it->second;
    }

    SampleZone zone;
    if (! readSampleZoneFromFile(file, rootMidi, 127, zone))
        return nullptr;

    auto ms = std::make_shared<Multisample>();
    ms->instrumentName = displayName.isNotEmpty() ? displayName : file.getFileNameWithoutExtension();
    ms->zones.push_back(std::move(zone));

    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        cache()[cacheKey] = ms;
    }

    return ms;
}

std::shared_ptr<const Multisample> SampleLibrary::loadMultisampleFromFiles(const juce::Array<juce::File>& files,
                                                                           const juce::String& displayName)
{
    if (files.isEmpty()) return nullptr;

    // Cache key: sorted absolute paths joined. Cheap dedupe across reloads.
    juce::StringArray paths;
    for (auto& f : files) paths.add(f.getFullPathName());
    paths.sort(true);
    const auto cacheKey = (juce::String("multi:") + paths.joinIntoString("|")).toStdString();

    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        auto it = cache().find(cacheKey);
        if (it != cache().end()) return it->second;
    }

    auto ms = buildMultisampleFromFiles(files, displayName);
    if (ms == nullptr) return nullptr;

    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        cache()[cacheKey] = ms;
    }
    return ms;
}

std::shared_ptr<const Multisample> SampleLibrary::loadMultisamplePreset(const juce::String& category,
                                                                        const juce::String& presetName,
                                                                        const juce::String& folderPath)
{
    juce::ignoreUnused(category);

    juce::File folder(folderPath);
    if (! folder.isDirectory()) return nullptr;

    // One physical preset folder is one source instrument. Different
    // .diapreset files may point at the same Guitar 1 folder, so cache by the
    // folder path only instead of the user preset name. This prevents every
    // guitar preset from loading a separate copy of the same WAV buffers.
    const auto cacheKey = (juce::String("folder:") + folder.getFullPathName()).toStdString();
    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        auto it = cache().find(cacheKey);
        if (it != cache().end()) return it->second;
    }

    auto files = folder.findChildFiles(juce::File::findFiles, true, "*.wav");
    if (files.isEmpty()) return nullptr;
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural(b.getFileName()) < 0;
    });

    auto ms = buildMultisampleFromFiles(files, presetName.isNotEmpty() ? presetName : folder.getFileName());
    if (ms == nullptr) return nullptr;

    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        cache()[cacheKey] = ms;
    }
    return ms;
}

} // namespace dida
