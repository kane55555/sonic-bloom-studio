#pragma once
//==============================================================================
//  UserPresetLoader.h
//
//  Parses ".diapreset" JSON files into UserPreset structs, scans the
//  Documents/DIDITAGAIN STUDIO/Samples/Presets/User/<Category>/ tree, and
//  applies a preset's parameter snapshot to the running AudioProcessor.
//
//  Sample folder loading itself is handled by the existing
//  SynthEngine::loadMultisamplePreset() path; this loader just routes the
//  request via PresetManager's requestedSampleFolderPath/category state.
//==============================================================================
#include <JuceHeader.h>
#include "UserPresetFormat.h"

namespace dida { namespace userpreset {

// Try to parse a .diapreset file. Returns false on malformed JSON or missing
// required fields (schemaVersion / presetName / sourceInstrument.path).
bool parseFile(const juce::File& file, UserPreset& out, juce::String& errorOut);

// Apply every block of a preset to the host AudioProcessor's APVTS. Does NOT
// load the sample folder itself — caller is responsible for routing
// `preset.source.path` into the SynthEngine.
void applyToProcessor(const UserPreset& preset, juce::AudioProcessor& proc);

// Additive load-time gain trim (dB) applied on top of preset.amp.gainDb inside
// applyToProcessor(). Currently only choir-mode presets receive a non-zero trim;
// every other preset returns 0 so its raw amp.gainDb is applied verbatim.
// Exposed so the preset-quality report can reproduce the engine's gain math
// deterministically instead of reading the (one-preset-late) live engine stage.
float loadTimeGainTrimDb(const UserPreset& preset);

// Serialise a preset to JSON text suitable for ".diapreset" on disk.
juce::String toJson(const UserPreset& preset);

// Resolve potentially-relative source paths against the user's Documents
// DIDITAGAIN STUDIO root. Returns the file as written if already absolute.
juce::File resolveSourcePath(const juce::String& rawPath);

// True when this preset is an AI Texture (cached neural) preset: it declares an
// enabled neuralTextureCached partial, or a cachedTexture/demoPack AI provider.
// Used by the preset-quality reporter to scope AI-Texture-only diagnostics and
// warning suppression.
bool isAiTexturePreset(const UserPreset& p) noexcept;

}} // namespace dida::userpreset
