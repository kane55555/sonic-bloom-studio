#pragma once
//==============================================================================
//  MacroMapper.h — Drives APVTS parameters from V2 preset macro targets.
//
//  Resolves macro target paths (e.g. "globalFilter.cutoff",
//  "effects.reverb.mix", "layers[1].volume", "glideTime", "monoMode")
//  to APVTS parameter IDs at preset-load time. apply() pushes the current
//  macro1..8 values onto the resolved parameters.
//
//  When a preset has no mappings, the processor falls back to its built-in
//  hardcoded macro behavior.
//==============================================================================
#include <JuceHeader.h>
#include "HybridPresetV2.h"

namespace dida { namespace preset {

class MacroMapper
{
public:
    struct ResolvedTarget
    {
        juce::String paramID;
        float minNorm = 0.0f;   // 0..1 normalized minimum
        float maxNorm = 1.0f;   // 0..1 normalized maximum
    };

    struct MacroBinding
    {
        int macroIndex = 0;     // 1..8
        juce::String displayName;
        std::vector<ResolvedTarget> targets;
    };

    void buildFrom(const HybridPresetV2& preset, juce::AudioProcessor& processor);
    void clear() { bindings.clear(); }
    bool isEmpty() const { return bindings.empty(); }

    /** Push current macroN APVTS values to all resolved targets. Cheap; safe per block. */
    void apply(juce::AudioProcessor& processor) const;

    /** Macro display name, or empty if none. */
    juce::String getMacroName(int macroIndex /*1..8*/) const;

private:
    std::vector<MacroBinding> bindings;
};

}} // namespace dida::preset
