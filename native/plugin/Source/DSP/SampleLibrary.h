#pragma once
//==============================================================================
//  SampleLibrary.h — Multisample (Nexus-style) instrument loader.
//
//  Drop audio one-shots into:
//      <UserDocuments>/DIDITAGAIN STUDIO/Samples/<InstrumentName>/
//
//  Filename convention (case-insensitive):
//      <anything>_<NoteName><Octave>[_v<Vel>].{wav,flac,ogg,mp3,aiff}
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
//  Playback uses linear-interpolated pitch-shift and crossfades the two
//  nearest root notes for smooth coverage across the keyboard.
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
    int loVel = 0;
    int hiVel = 127;
    juce::String fileName;
};

class Multisample
{
public:
    std::vector<SampleZone> zones;
    juce::String instrumentName;

    // Find the two nearest zones for crossfade. Returns:
    //   *lower : zone with rootMidi <= midi (or nearest)
    //   *upper : zone with rootMidi >  midi (or null if none above)
    //   xfade  : 0..1, weight of the upper zone (0 = use only lower)
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

    // Force a rescan (clears the in-memory cache).
    static void invalidateCache();

private:
    SampleLibrary() = delete;
};

} // namespace dida
