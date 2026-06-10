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

// --- Blend mode (v2.1 additive) ---
// Describes HOW a synth/oscillator layer is intended to sit relative to the
// main PCM source. Drives sensible defaults for max gain, EQ carving and
// envelope coupling so a "reinforcement" layer never escapes as a beep on
// top of a sampled instrument.
//
// Recognised values (case-insensitive):
//   "reinforceBody" "addWarmth" "addAir" "subSupport"
//   "hiddenTexture" "leadLayer" ""(empty => engine picks per-category)
struct LayerBlock
{
    bool  enabled    = true;
    float gainDb     = 0.0f;
    float pan        = 0.0f;
    int   octave     = 0;
    int   semitone   = 0;
    float detuneCents = 0.0f;

    // v2.1 blend-mode contract (all optional; engine fills in defaults).
    juce::String blendMode;          // "" => category default
    juce::String eqRole;             // "body" | "warmth" | "air" | "sub" | "texture" | "" (auto)
    bool         followMainEnvelope = true;
    float        maxGainDb = 0.0f;   // 0 = use category default cap
};

struct FxChorusBlock   { bool enabled=false; float rateHz=0.4f; float depth=0.25f; float mix=0.0f; };
struct FxDelayBlock    { bool enabled=false; float timeMs=300.0f; float feedback=0.3f; float mix=0.0f; bool hasFeedback=false; };
struct FxReverbBlock
{
    bool  enabled  = false;
    float size     = 0.5f;
    float damping  = 0.4f;
    float mix      = 0.0f;
    bool  hasMix   = false;             // any of mix/reverbMix/wet present
    bool  bypass   = false;            // bypass==true forces reverb output to silence
    float preDelayMs       = -1.0f;    // <0 => not set by preset
    bool  hasDucking       = false;
    bool  duckingEnabled   = true;
    float duckingAmount    = -1.0f;    // <0 => not set by preset
    float inputHighpassHz  = -1.0f;    // <0 => not set by preset
    float inputLowpassHz   = -1.0f;    // <0 => not set by preset
};
struct FxSatBlock      { bool enabled=false; float drive=0.0f; float mix=0.0f; };

struct FxSendBlock
{
    bool  hasFxSendReleaseMs = false;
    float fxSendReleaseMs = 80.0f;
    bool  hasFxSendMaximumReleaseMs = false;
    float fxSendMaximumReleaseMs = 180.0f;
    bool  hasFxSendReleaseMultiplier = false;
    float fxSendReleaseMultiplier = 0.35f;
    bool  noteOffStopsFxSend = true;
    bool  fxSendFollowsAmpEnvelope = true;
    // Send-level aliases. sendGainDb / reverbSendDb both control the reverb
    // send; delaySendDb controls the delay send.
    bool  hasReverbSendDb = false; float reverbSendDb = 0.0f;
    bool  hasDelaySendDb  = false; float delaySendDb  = 0.0f;
};

struct SafetyBlock
{
    bool  hasChoirFxSendReleaseMaxMs = false;
    float choirFxSendReleaseMaxMs = 180.0f;
};

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

// ------- Modulation matrix (v2 optional) -------
// Each entry routes a normalized source value to a destination, scaled by
// `amount` (-1..1). Recognized sources: "env1","env2","lfo1","lfo2",
// "velocity","modwheel","aftertouch","keytrack". Recognized destinations:
// "filter1Cutoff","filter1Reso","amp.gain","amp.pan","osc.pitch",
// "osc.pulseWidth","fxReverbMix","fxDelayMix","fxChorusMix". Unknown
// (src,dest) pairs are loaded and serialized round-trip but produce no
// audible effect.
struct ModMatrixEntry
{
    juce::String source;
    juce::String dest;
    float        amount  = 0.0f;
    bool         bipolar = true;
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
    FxSendBlock   fxSend;
    SafetyBlock   safety;

    LfoBlock lfo1;
    LfoBlock lfo2;

    AdvancedBlock advanced;

    // v2 additions (all optional in JSON, defaults are sensible)
    MacroBlock          macros;
    VelocityBlock       velocity;
    LayerEqCarveBlock   layerEq;
    FilterMovementBlock filterMovement;
    bool                experimental = false;
    bool                choirMode = false;

    // Optional mod-matrix routings. Empty by default; preserved across
    // load/save even when entries aren't yet wired to engine destinations.
    juce::Array<ModMatrixEntry> modMatrix;

    // ------- Multi-engine partials (v2 additive) -------
    // Optional. If omitted, the plugin uses the legacy PCM/multisample path
    // exactly as before (full backwards-compat for every existing preset).
    //
    // Each partial can pick its own engine ("pcm" | "analog" | "supersaw"
    // | "fm" | "wavetable" | "granular") and gets its own filter, amp env,
    // LFO, mod-matrix slots and pan/level. Up to 4 partials per tone.
    //
    // The default top-level `engineType` provides a hint for tools/UI when
    // no partials are defined; absent => "pcm".
    struct PartialBlock
    {
        bool          enabled    = false;
        juce::String  engineType = "pcm";
        float         level      = 1.0f;
        float         pan        = 0.0f;
        int           pitchSemis = 0;
        float         fineCents  = 0.0f;
        juce::var     engineParams;
        FilterBlockData filter;
        AmpBlock        amp;
        LfoBlock        lfo;
        juce::Array<ModMatrixEntry> mods;

        // v2.1 blend-mode contract — see LayerBlock above for the value list.
        juce::String blendMode;
        juce::String eqRole;
        bool         followMainEnvelope = true;
        float        maxGainDb = 0.0f;

        // AI Texture demo-pack schema additive: some packs declare a neural
        // texture partial with a top-level "levelDb" (instead of nesting it in
        // engineParams) and a redundant "isNeuralTexture" hint. Both optional.
        bool         hasLevelDb = false;
        float        levelDb    = 0.0f;
        bool         isNeuralTexture = false;
    };

    juce::String              engineType;   // optional; default "" => pcm
    juce::Array<PartialBlock> partials;     // optional; up to 4

    // ------- AI Texture v0.1 (cached) metadata (v2 additive) -------
    // Optional. Describes an offline-analysed timbre profile plus the cached
    // neural texture provider. v0.1 only CONSUMES cached WAV textures referenced
    // by a neuralTextureCached partial — there is NO realtime neural inference.
    //
    //   ai.enabled .................. master switch (default false => disabled)
    //   ai.profileVersion ........... DDSP/RAVE profile schema revision
    //   ai.provider ................. "ddsp" | "rave" | "" (analysis source)
    //   ai.analysisFile ............. path to the offline DDSP analysis json
    //   ai.textureMode .............. "cached" (only supported mode in v0.1)
    //   ai.timbreProfile.* .......... 0..1 descriptors from offline analysis
    //
    // A MISSING ai block parses to AiBlock{} which defaults to disabled, so
    // every existing .diapreset behaves exactly as before.
    struct AiTimbreProfile
    {
        float brightness       = 0.5f;
        float harmonicDensity  = 0.5f;
        float noiseAir         = 0.5f;
        float attackNoise      = 0.5f;
        float pitchInstability = 0.0f;
        float bodyWarmth       = 0.5f;
    };

    struct AiBlock
    {
        bool         present        = false;   // true when JSON contained an "ai" object
        bool         enabled        = false;
        int          profileVersion = 0;
        juce::String provider;                 // "ddsp" | "rave" | ""
        juce::String analysisFile;             // offline DDSP analysis path
        juce::String textureMode   = "cached"; // only "cached" supported in v0.1
        AiTimbreProfile timbreProfile;
    };

    AiBlock ai;
};

}} // namespace dida::userpreset
