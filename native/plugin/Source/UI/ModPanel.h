#pragma once
#include "ParameterPanel.h"

class ModPanel : public ParameterPanel
{
public:
    explicit ModPanel(juce::AudioProcessorValueTreeState& apvts)
        : ParameterPanel(apvts, "MODULATION")
    {
        const juce::StringArray shapes { "Sine", "Triangle", "Saw", "Square", "S&H" };

        const int lfo1 = addGroup("LFO 1");
        addKnob(lfo1, "Rate", "lfo1Rate");
        addChoice(lfo1, "Shape", "lfo1Shape", shapes);
        addToggle(lfo1, "Sync", "lfo1Sync");

        const int lfo2 = addGroup("LFO 2");
        addKnob(lfo2, "Rate", "lfo2Rate");
        addChoice(lfo2, "Shape", "lfo2Shape", shapes);
        addToggle(lfo2, "Sync", "lfo2Sync");

        const int modEnv = addGroup("MOD ENV");
        addKnob(modEnv, "Attack", "env3Attack");
        addKnob(modEnv, "Decay", "env3Decay");
        addKnob(modEnv, "Sustain", "env3Sustain");
        addKnob(modEnv, "Release", "env3Release");

        const int unison = addGroup("UNISON / PLAY");
        addKnob(unison, "Voices", "unisonVoices");
        addKnob(unison, "Detune", "unisonDetune");
        addKnob(unison, "Spread", "unisonSpread");
        addKnob(unison, "Glide", "glideTime");

        const int macrosA = addGroup("MACROS 1-4");
        addKnob(macrosA, "Macro 1", "macro1");
        addKnob(macrosA, "Macro 2", "macro2");
        addKnob(macrosA, "Macro 3", "macro3");
        addKnob(macrosA, "Macro 4", "macro4");

        const int macrosB = addGroup("MACROS 5-8");
        addKnob(macrosB, "Macro 5", "macro5");
        addKnob(macrosB, "Macro 6", "macro6");
        addKnob(macrosB, "Macro 7", "macro7");
        addKnob(macrosB, "Macro 8", "macro8");
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModPanel)
};