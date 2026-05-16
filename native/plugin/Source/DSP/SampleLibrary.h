#pragma once
//==============================================================================
//  SampleLibrary.h — Multisample (Nexus-style) instrument loader.
//
//  Drop audio one-shots into:
//      <UserDocuments>/DIDITAGAIN STUDIO/Samples/<InstrumentName>/
//
//  Filename convention (case-insensitive):
//      <anything>_<NoteName><Octave>[_v<Vel>].wav
//
//      Brass_C3.wav
//      Brass_F#3_v90.wav
//      Brass_Bb4_v40.wav
//
//  NoteName: C, C#, Db, D, D#, Eb, E, F, F#, Gb, G, G#, Ab, A, A#, Bb, B
//  Octave  : -1 .. 9     (C4 = MIDI 60)
//  Vel     : 1..127      (upper bound of velocity layer; optional)
//
//  Notes without an explicit velocity tag cover the full 0..127 range as a
//  fallback layer. When multiple velocity layers exist for the same note, the
//  layer whose hiVel is the smallest value >= playedVel is selected.
//
//  Playback uses hard key zones: C covers C-C#, D# covers D-E,
//  F# covers F-G, and A covers G#-B for each octave.
//==============================================================================
#include <JuceHeader.h>
#include <memory>
#include <vector>
#include <unordered_map>

namespace dida {

struct SampleZone
{
    juce::AudioBuffer<float> buffer;     // always stereo internally
    double sourceSampleRate = 44100.0;
    int rootMidi = 60;
    int lowKey = 60;
    int highKey = 60;
    int loVel = 0;
    int hiVel = 127;
    juce::String fileName;
};

class Multisample
{
public:
    std::vector<SampleZone> zones;
    juce::String instrumentName;

    // Find the hard key-zone for this MIDI note. Returns the matching zone in
    // *lower and leaves *upper null. If no lowKey/highKey range matches, the
    // nearest root is returned as an explicit fallback.
    void pickZonesForNote(int midi, int velocity,
                          const SampleZone** lower,
                          const SampleZone** upper,
                          float& xfade) const noexcept;

    bool isEmpty() const noexcept { return zones.empty(); }
};

class SampleLibrary
{
public:
    // The root samples folder: <UserDocuments>/DIDITAGAIN STUDIO/Samples
    static juce::File getSamplesRoot();

    // Names of every sub-folder (instrument) that contains at least one audio file.
    static juce::StringArray listInstruments();

    // Loads (and caches) an instrument folder. Thread-safe; returns nullptr if
    // the folder is missing or contains no usable samples.
    static std::shared_ptr<const Multisample> loadInstrument(const juce::String& name);

    // Loads one exact sample referenced by a generated hybrid preset. This is
    // the user-import path: the preset owns a specific source file instead of
    // depending on "folder name = instrument" discovery.
    static std::shared_ptr<const Multisample> loadSampleSource(const juce::String& sourcePath,
                                                              int rootMidi,
                                                              const juce::String& displayName = {});

    // Build a Multisample from an explicit list of audio files. Root MIDI is
    // parsed from each filename's trailing note token (e.g. "_C3", "_F#4").
    // Files without a parseable note token are skipped. Used by the preset
    // browser to play a user-dropped sub-folder ("Guitars/Guitar 1/") as a
    // single multisampled instrument with one WAV per hard key zone.
    static std::shared_ptr<const Multisample> loadMultisampleFromFiles(const juce::Array<juce::File>& files,
                                                                       const juce::String& displayName);

    // True folder-preset loader: one preset folder equals one instrument, and
    // every parseable WAV inside the folder becomes one hard key zone.
    static std::shared_ptr<const Multisample> loadMultisamplePreset(const juce::String& category,
                                                                    const juce::String& presetName,
                                                                    const juce::String& folderPath);

    // Force a rescan (clears the in-memory cache).
    static void invalidateCache();

private:
    SampleLibrary() = delete;
};

} // namespace dida
