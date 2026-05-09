#pragma once
//==============================================================================
//  PresetSchema.h — Canonical .didasynthpreset JSON shape, version
//  constants, and helpers for mapping between JSON keys and
//  AudioProcessorValueTreeState parameter IDs.
//
//  Schema version:
//    1 — initial release (this version)
//==============================================================================
#include <JuceHeader.h>
#include <unordered_map>
#include <string>

namespace dida { namespace preset {

constexpr int kSchemaVersion = 1;
constexpr const char* kFileExtension = ".didasynthpreset";

// JSON top-level keys.
namespace key {
    constexpr const char* presetVersion = "presetVersion";
    constexpr const char* presetName    = "presetName";
    constexpr const char* author        = "author";
    constexpr const char* category      = "category";
    constexpr const char* tags          = "tags";
    constexpr const char* description   = "description";
    constexpr const char* engineMode    = "engineMode";
    constexpr const char* sampler       = "sampler";    // { "instrument": "Brass" }
    constexpr const char* instrument    = "instrument"; // string under "sampler"
    constexpr const char* masterGain    = "masterGain";
    constexpr const char* polyphony     = "polyphony";
    constexpr const char* mono          = "mono";
    constexpr const char* glideMs       = "glideMs";
    constexpr const char* glideTime     = "glideTime";
    constexpr const char* playMode      = "playMode";
    constexpr const char* oscA          = "oscA";
    constexpr const char* oscB          = "oscB";
    constexpr const char* subOsc        = "subOsc";
    constexpr const char* noise         = "noise";
    constexpr const char* filter1       = "filter1";
    constexpr const char* env1          = "env1";
    constexpr const char* env2          = "env2";
    constexpr const char* env3          = "env3";
    constexpr const char* lfo1          = "lfo1";
    constexpr const char* lfo2          = "lfo2";
    constexpr const char* modMatrix     = "modMatrix";
    constexpr const char* fxChain       = "fxChain";
    constexpr const char* macroKnobs    = "macroKnobs";
    constexpr const char* tuning        = "tuning";
    constexpr const char* checksum      = "checksum";
    constexpr const char* signature     = "signature";
}

// Convert a string engine mode (as stored in JSON) to its choice index.
inline int engineModeFromString(const juce::String& s)
{
    auto u = s.toLowerCase();
    if (u == "fm2"  || u == "fm2op" || u == "fm")        return 1;
    if (u == "fm4"  || u == "fm4op")                     return 2;
    if (u == "wavetable" || u == "wt")                   return 3;
    if (u == "layered")                                  return 4;
    return 0; // Subtractive
}

inline int waveformFromString(const juce::String& s)
{
    auto u = s.toLowerCase();
    if (u == "sine")      return 0;
    if (u == "triangle")  return 1;
    if (u == "saw")       return 2;
    if (u == "square")    return 3;
    if (u == "pulse")     return 4;
    if (u == "supersaw")  return 5;
    if (u == "fm" || u == "fmcarrier") return 6;
    if (u == "wavetable") return 7;
    return 2; // saw default
}

inline int filterTypeFromString(const juce::String& s)
{
    if (s.equalsIgnoreCase("LP12"))  return 0;
    if (s.equalsIgnoreCase("LP24"))  return 1;
    if (s.equalsIgnoreCase("HP12"))  return 2;
    if (s.equalsIgnoreCase("HP24"))  return 3;
    if (s.equalsIgnoreCase("BP"))    return 4;
    if (s.equalsIgnoreCase("Notch")) return 5;
    return 1;
}

}} // namespace dida::preset
