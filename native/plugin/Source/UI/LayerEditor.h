#pragma once
#include "ParameterPanel.h"

// Hybrid 4-layer editor (Phase 5).
// Each layer maps to an existing APVTS source until the dedicated layer DSP
// (Phase 3) is wired:
//   Layer 1 - MAIN SAMPLE  -> OSC A
//   Layer 2 - SINE BODY    -> OSC B
//   Layer 3 - AIR / NOISE  -> Noise
//   Layer 4 - SHIMMER / SUB-> Sub osc
// This gives the UI the Zenology-style per-layer surface immediately.
class LayerEditor : public ParameterPanel
{
public:
    explicit LayerEditor(juce::AudioProcessorValueTreeState& apvts)
        : ParameterPanel(apvts, "HYBRID LAYERS")
    {
        const juce::StringArray waves { "Sine", "Triangle", "Saw", "Square", "Pulse", "SuperSaw", "FmCarrier", "Wavetable" };
        const juce::StringArray noise { "White", "Pink" };

        const int main = addGroup("LAYER 1 - MAIN SAMPLE");
        addChoice(main, "Wave",   "oscAWaveform", waves);
        addKnob  (main, "Level",  "oscALevel");
        addKnob  (main, "Oct",    "oscAOctave");
        addKnob  (main, "Semi",   "oscASemi");
        addKnob  (main, "Detune", "oscADetune");

        const int body = addGroup("LAYER 2 - SINE BODY");
        addChoice(body, "Wave",   "oscBWaveform", waves);
        addKnob  (body, "Level",  "oscBLevel");
        addKnob  (body, "Oct",    "oscBOctave");
        addKnob  (body, "Semi",   "oscBSemi");
        addKnob  (body, "Detune", "oscBDetune");

        const int air = addGroup("LAYER 3 - AIR / NOISE");
        addChoice(air, "Type",  "noiseType", noise);
        addKnob  (air, "Level", "noiseLevel");

        const int shimmer = addGroup("LAYER 4 - SHIMMER / SUB");
        addToggle(shimmer, "Enable", "subOscEnabled");
        addKnob  (shimmer, "Level",  "subOscLevel");

        const int common = addGroup("LAYER COMMON");
        addKnob(common, "Glide",   "glideTime");
        addKnob(common, "Attack",  "env1Attack");
        addKnob(common, "Decay",   "env1Decay");
        addKnob(common, "Sustain", "env1Sustain");
        addKnob(common, "Release", "env1Release");
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LayerEditor)
};
