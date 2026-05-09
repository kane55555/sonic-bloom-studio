#pragma once
//==============================================================================
//  FactoryPresets.h — Embedded 30 factory presets shipped with the plugin.
//
//  Each preset is a self-contained JSON string conforming to PresetSchema.h.
//  These are extracted on first run to the user's factory preset directory
//  so the standard JSON loader can handle them uniformly.
//==============================================================================
#include <JuceHeader.h>
#include <vector>

namespace dida { namespace factory {

struct EmbeddedPreset
{
    const char* name;
    const char* category;
    const char* json;
};

// Returns all 30 factory presets.
const std::vector<EmbeddedPreset>& getAll();

// Writes any missing factory presets to `targetDir`. Returns count written.
int extractMissing(const juce::File& targetDir);

}} // namespace dida::factory
