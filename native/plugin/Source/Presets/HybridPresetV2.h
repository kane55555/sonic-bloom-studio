#pragma once
//==============================================================================
//  HybridPresetV2.h — In-memory representation of a schemaVersion 2.0.0
//  .didasynthpreset file. Mirrors packages/preset-schema/src/presetTypes.ts.
//
//  This struct is intentionally permissive: unknown fields are kept as raw
//  juce::var so round-tripping and forward-compat work cleanly.
//==============================================================================
#include <JuceHeader.h>
#include <vector>

namespace dida { namespace preset {

constexpr const char* kSchemaVersionV2 = "2.0.0";

enum class LayerType { Sample, Oscillator, Noise, Texture };

struct AmpEnvV2 { float attack=0.001f, decay=0.5f, sustain=0.7f, release=1.0f; };
struct LayerFilterV2 {
    bool enabled = true;
    juce::String type = "lowpass";
    float cutoff = 9000.0f;
    float resonance = 0.15f;
    float drive = 0.0f;
};

struct LayerV2
{
    juce::String id;
    juce::String name;
    LayerType    type = LayerType::Sample;
    bool         enabled = true;
    float        volume = 0.8f;
    float        pan = 0.0f;
    AmpEnvV2     ampEnv;
    LayerFilterV2 filter;
    bool         hasFilter = false;

    // Sample-layer fields (valid when type == Sample):
    juce::String source;            // relative path under DIDITAGAIN STUDIO/
    juce::String rootNote = "C4";
    int          rootMidi = 60;
    bool         pitchTracking = true;
    bool         oneShotMode = false;
    int          pitchSemis = 0;
    int          fineCents = 0;
    bool         reverse = false;
    bool         loop = false;

    // Crop/loop (0..1 fractions of audio length)
    float        cropStart = 0.0f, cropEnd = 1.0f;
    float        loopStart = 0.2f, loopEnd = 0.95f;
    float        loopCrossfadeMs = 20.0f;
    bool         autoLoop = true;

    // Oscillator-layer fields:
    juce::String waveform = "sine";
};

struct EffectsV2
{
    bool eqEnabled = true;        float eqLowCut = 80.0f, eqBody = 0.0f, eqPresence = 0.0f, eqAir = 0.0f;
    bool satEnabled = false;      juce::String satMode = "tape"; float satDrive = 0.1f, satMix = 0.25f;
    bool chorusEnabled = false;   float chorusRate = 0.3f, chorusDepth = 0.2f, chorusMix = 0.2f;
    bool delayEnabled = false;    bool delaySync = true; juce::String delayTime = "1/4"; float delayFb = 0.25f, delayMix = 0.15f;
    bool reverbEnabled = true;    float reverbSize = 0.5f, reverbDecay = 2.0f, reverbMix = 0.2f;
    bool widthEnabled = true;     float widthAmount = 0.3f;
    bool limiterEnabled = true;   float limiterCeiling = -0.5f;
};

struct MacroTargetV2 { juce::String path; float min = 0.0f, max = 1.0f; };
struct MacroV2 {
    juce::String id, name;
    float value = 0.5f;
    std::vector<MacroTargetV2> targets;
};

struct HybridPresetV2
{
    juce::String schemaVersion = kSchemaVersionV2;
    juce::String presetId;
    juce::String name;
    juce::String bank = "User";       // "Factory" | "User"
    juce::String category = "Uncategorized";
    juce::String subCategory;
    juce::String author = "User";
    juce::String dateCreated, dateModified;
    juce::StringArray tags;
    juce::StringArray genre;
    juce::StringArray mood;

    // sourceImport
    bool         hasSourceImport = false;
    juce::String sourceOriginalFileName;
    juce::String sourceSamplePath;
    juce::String sourceRootNote;
    int          sourceRootMidi = 60;
    juce::String sourceRootSrc = "manual";
    bool         sourcePitchTracking = true;

    // quality
    bool needsReview = false;
    bool rootNoteVerified = true;

    std::vector<LayerV2> layers;
    LayerFilterV2 globalFilter;
    EffectsV2     effects;
    std::vector<MacroV2> macros;
};

}} // namespace dida::preset
