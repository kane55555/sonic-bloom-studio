#pragma once
//==============================================================================
//  PresetMigration.h — Convert legacy v1 .didasynthpreset JSON into the v2
//  hybrid layered structure. Used transparently by PresetManager so existing
//  factory and user presets keep loading.
//==============================================================================
#include <JuceHeader.h>
#include "HybridPresetV2.h"

namespace dida { namespace preset {

class PresetMigration
{
public:
    /** Returns true if `json` looks like a v1 (legacy) preset file. */
    static bool isLegacy(const juce::var& json) noexcept;

    /** Build a v2 preset from a parsed v1 JSON. */
    static HybridPresetV2 toV2(const juce::var& legacyJson);

    /** Parse any .didasynthpreset (v1 or v2) into HybridPresetV2.
        Returns false if `json` is not a valid preset object. */
    static bool parseAny(const juce::var& json, HybridPresetV2& outPreset);
};

}} // namespace dida::preset
