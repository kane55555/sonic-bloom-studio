#pragma once
//==============================================================================
//  HybridPresetApplier.h — V2 (HybridPresetV2) -> engine mapper.
//
//  Translates a parsed HybridPresetV2 into APVTS parameter writes plus a
//  small `Result` describing how the SynthEngine should be configured
//  (sample source, looping, fallback synth, mono mode, glide, macro map).
//
//  PresetManager.cpp delegates V2 loads here. PluginProcessor consumes the
//  Result on its next block to apply the changes safely.
//==============================================================================
#include <JuceHeader.h>
#include "HybridPresetV2.h"

namespace dida { namespace preset {

struct AppliedPresetState
{
    bool         hasSample           = false;
    juce::String sampleSource;
    int          sampleRootMidi      = 60;
    juce::String displayName;
    bool         shouldLoop          = false;
    bool         monoMode            = false;
    juce::String category;
    HybridPresetV2 preset;            // kept for macro mapper / UI
};

class HybridPresetApplier
{
public:
    /** Apply all V2 layers, global filter and effects to APVTS. Returns
        the engine-side state that the processor should propagate next block. */
    static AppliedPresetState apply(const HybridPresetV2& p,
                                    juce::AudioProcessor& processor);

    /** Per-category looping decision. Honors layer.loop and oneShotMode. */
    static bool shouldLoopForCategory(const juce::String& category,
                                      bool oneShotMode,
                                      bool layerLoop);
};

}} // namespace dida::preset
