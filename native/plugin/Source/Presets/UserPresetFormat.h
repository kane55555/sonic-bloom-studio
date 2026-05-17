#pragma once
//==============================================================================
//  UserPresetFormat.h
//
//  In-memory model for ".diapreset" JSON files. One preset references a
//  multisample folder (e.g. Guitars/Guitar 1) plus a full snapshot of
//  amp/filter/layer/FX/modulation/advanced settings.
//
//  The full JSON schema is documented at the top of UserPresetLoader.cpp.
//==============================================================================
#include <JuceHeader.h>

namespace dida { namespace userpreset {

inline constexpr const char* kFileExtension = ".diapreset";
inline constexpr int kSchemaVersion = 1;

struct SourceInstrument
{
    juce::String type;             // "multisampleFolder" | "singleSample"
    juce::String path;             // absolute or {DocsRoot}-relative
    juce::String mappingMode;      // "hardZones" | "nearest"
    juce::StringArray rootNotePattern;
};

struct AmpBlock
{
    float gainDb     = 0.0f;
    float pan        = 0.0f;
    float attackMs   = 5.0f;
    float decayMs    = 200.0f;
    float sustain    = 0.9f;
    float releaseMs  = 400.0f;
};

struct FilterBlockData
{
    bool  enabled    = true;
    juce::String type = "lowpass";
    float cutoffHz   = 18000.0f;
    float resonance  = 0.1f;
    float drive      = 0.0f;
    float keytrack   = 0.0f;
};

struct LayerBlock
{
    bool  enabled    = true;
    float gainDb     = 0.0f;
    float pan        = 0.0f;
    int   octave     = 0;
    int   semitone   = 0;
    float detuneCents = 0.0f;
};

struct FxChorusBlock   { bool enabled=false; float rateHz=0.4f; float depth=0.25f; float mix=0.0f; };
struct FxDelayBlock    { bool enabled=false; float timeMs=300.0f; float feedback=0.3f; float mix=0.0f; };
struct FxReverbBlock   { bool enabled=false; float size=0.5f; float damping=0.4f; float mix=0.0f; };
struct FxSatBlock      { bool enabled=false; float drive=0.0f; float mix=0.0f; };

struct LfoBlock
{
    bool  enabled = false;
    juce::String target;
    juce::String shape = "sine";
    float rateHz = 1.0f;
    float depth  = 0.0f;
};

struct AdvancedBlock
{
    float sampleStartMs       = 0.0f;
    float randomStartMs       = 0.0f;
    float velocityToGain      = 0.25f;
    float velocityToCutoff    = 0.0f;
    float humanizePitchCents  = 0.0f;
    float humanizeTimingMs    = 0.0f;
    int   polyphony           = 16;
};

struct UserPreset
{
    int          schemaVersion = kSchemaVersion;
    juce::String presetName;
    juce::String category;

    SourceInstrument source;
    AmpBlock         amp;
    FilterBlockData  filter;
    LayerBlock       main;
    LayerBlock       layer2 { false, -12.0f, 0.0f, 1, 0, -5.0f };

    FxChorusBlock chorus;
    FxDelayBlock  delay;
    FxReverbBlock reverb;
    FxSatBlock    saturation;

    LfoBlock lfo1;
    LfoBlock lfo2;

    AdvancedBlock advanced;
};

}} // namespace dida::userpreset
