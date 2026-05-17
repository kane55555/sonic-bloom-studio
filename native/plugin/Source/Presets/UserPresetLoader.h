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

// Serialise a preset to JSON text suitable for ".diapreset" on disk.
juce::String toJson(const UserPreset& preset);

// Resolve potentially-relative source paths against the user's Documents
// DIDITAGAIN STUDIO root. Returns the file as written if already absolute.
juce::File resolveSourcePath(const juce::String& rawPath);

}} // namespace dida::userpreset
