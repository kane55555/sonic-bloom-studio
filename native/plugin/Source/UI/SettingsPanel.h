#pragma once
#include "ParameterPanel.h"

class SettingsPanel : public ParameterPanel
{
public:
    explicit SettingsPanel(juce::AudioProcessorValueTreeState& apvts)
        : ParameterPanel(apvts, "SETTINGS")
    {
        const int playback = addGroup("PLAYBACK");
        addChoice(playback, "Quality", "qualityMode", { "Draft", "Standard", "High" });
        addKnob(playback, "Polyphony", "polyphony");
        addToggle(playback, "Mono", "monoMode");
        addKnob(playback, "Glide", "glideTime");

        const int master = addGroup("MASTER SAFETY");
        addKnob(master, "Gain", "masterGain");
        addKnob(master, "Ceiling", "limiterCeiling");
        addToggle(master, "Compressor", "compEnabled");
        addKnob(master, "Threshold", "compThreshold");
        addKnob(master, "Ratio", "compRatio");
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPanel)
};