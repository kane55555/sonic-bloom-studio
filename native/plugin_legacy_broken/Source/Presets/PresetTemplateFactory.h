#pragma once
//==============================================================================
//  PresetTemplateFactory.h — In-plugin "New Preset from Template" helper.
//  Mirrors packages/preset-schema/src/categoryTemplates.ts so the plugin can
//  build a fresh hybrid preset for any category without round-tripping JSON
//  through the Python importer.
//==============================================================================
#include <JuceHeader.h>
#include "HybridPresetV2.h"

namespace dida { namespace preset {

class PresetTemplateFactory
{
public:
    /** Make a brand-new preset for `category`, optionally pointing Layer 1
        at a sample asset on disk. `category` should be one of the producer-
        facing names (DrillBells, Bass808, etc.). Falls back to Uncategorized. */
    static HybridPresetV2 build(const juce::String& category,
                                const juce::String& presetName,
                                const juce::String& sampleRelPath = {},
                                int rootMidi = 72,
                                const juce::String& rootNote = "C5");
};

}} // namespace dida::preset
