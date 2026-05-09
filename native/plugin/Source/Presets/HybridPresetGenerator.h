#pragma once
//==============================================================================
//  HybridPresetGenerator.h — Build a v2 preset from an in-plugin sample
//  asset. Used when the user drops a sample inside the plugin (rather than
//  via the Python CLI).
//==============================================================================
#include <JuceHeader.h>
#include "HybridPresetV2.h"

namespace dida { namespace preset {

class HybridPresetGenerator
{
public:
    struct Inputs {
        juce::String category;        // e.g. "DrillBells"
        juce::String presetName;
        juce::String sampleRelPath;   // under DIDITAGAIN STUDIO/
        juce::String originalFileName;
        juce::String rootNote = "C5";
        int          rootMidi = 72;
        bool         pitchTracking = true;
        bool         oneShotMode = false;
        bool         needsReview = false;
        juce::String rootNoteSource = "manual"; // filename | pitch-detect | guessed | manual
    };

    static HybridPresetV2 generate(const Inputs& in);

    /** Serialise a HybridPresetV2 back to JSON text. */
    static juce::String toJsonString(const HybridPresetV2& preset);
};

}} // namespace dida::preset
