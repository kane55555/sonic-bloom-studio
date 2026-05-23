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
    // Crop/loop
    float        cropStart       = 0.0f;
    float        cropEnd         = 1.0f;
    float        loopStart       = 0.2f;
    float        loopEnd         = 0.95f;
    float        loopCrossfadeMs = 20.0f;
    bool         oneShotMode     = false;
    bool         pitchTracking   = true;
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

/** Voices the reverb tank (HP/LP, diffusion, ducking, low-mono, width,
    character) to match a preset category. Used by both V2 (HybridPreset)
    and V1 (.diapreset) load paths so all presets get the cinematic voicing. */
void applyReverbCharacterForCategory(juce::AudioProcessor& processor,
                                     const juce::String& category);

}} // namespace dida::preset
