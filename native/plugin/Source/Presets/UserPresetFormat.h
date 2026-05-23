#pragma once
//==============================================================================
//  UserPresetFormat.h
//
//  In-memory model for ".diapreset" JSON files. One preset references a
//  multisample folder (e.g. Guitars/Guitar 1) plus a full snapshot of
//  amp/filter/layer/FX/modulation/advanced settings.
//
//  Schema v1 -> v2 (backwards compatible — all new blocks are optional)
//    v2 adds:
//      - "macros":    { tone, movement, width, warmth, attack, release,
//                       space, character }              (0..1)
//      - "velocity":  { toGain, toCutoff, toAttack, toLayerBlend }  (0..1)
//      - "layerEq":   { mainBodyHz, mainAirHz,
//                       layer2BodyHz, layer2AirHz }     (Hz; 0 = bypass)
//      - "filterMovement": { enabled, depth, rateHz }   (subtle LFO -> cutoff)
//      - "experimental": bool (if true, big modulation/wobble is allowed)
//
//  The full JSON schema is documented at the top of UserPresetLoader.cpp.
//==============================================================================
#include <JuceHeader.h>

namespace dida { namespace userpreset {

inline constexpr const char* kFileExtension = ".diapreset";
inline constexpr int kSchemaVersion = 2;

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

// ------- v2 additions -------

struct MacroBlock
{
    // Premium preset descriptors — all 0..1.
    float tone      = 0.5f;   // dark -> bright (tilts EQ/filter)
    float movement  = 0.3f;   // adds slow shared modulation depth
    float width     = 0.6f;   // stereo width of layer bus
    float warmth    = 0.4f;   // analog saturation amount on layer bus
    float attack    = 0.3f;   // global env attack scaler
    float release   = 0.5f;   // global env release scaler
    float space     = 0.4f;   // reverb mix scaler
    float character = 0.5f;   // reverb character bias (vintage <-> shimmer)
};

struct VelocityBlock
{
    float toGain        = 0.35f;
    float toCutoff      = 0.20f;
    float toAttack      = 0.10f;   // soft notes => slightly slower attack
    float toLayerBlend  = 0.30f;   // soft notes => quieter air/shimmer layers
};

struct LayerEqCarveBlock
{
    // 0 = bypassed. Hz cutoffs for per-layer carving so layers don't clash.
    float mainBodyHz   = 0.0f;     // optional LPF on main layer (warmth)
    float mainAirHz    = 0.0f;     // optional HPF on main layer (rare)
    float layer2BodyHz = 220.0f;   // HPF on layer 2 -> air only
    float layer2AirHz  = 0.0f;     // optional LPF on layer 2
};

struct FilterMovementBlock
{
    bool  enabled = false;
    float depth   = 0.15f;         // 0..1, subtle by default
    float rateHz  = 0.20f;
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

    // v2 additions (all optional in JSON, defaults are sensible)
    MacroBlock          macros;
    VelocityBlock       velocity;
    LayerEqCarveBlock   layerEq;
    FilterMovementBlock filterMovement;
    bool                experimental = false;
};

}} // namespace dida::userpreset
