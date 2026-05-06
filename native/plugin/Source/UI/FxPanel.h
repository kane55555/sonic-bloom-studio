#pragma once
#include "ParameterPanel.h"

class FxPanel : public ParameterPanel
{
public:
    explicit FxPanel(juce::AudioProcessorValueTreeState& apvts)
        : ParameterPanel(apvts, "EFFECTS / MASTER")
    {
        const int tone = addGroup("TONE / EQ");
        addKnob(tone, "Drive", "fxDistortionAmount");
        addKnob(tone, "Low", "eqLow");
        addKnob(tone, "Mid", "eqMid");
        addKnob(tone, "High", "eqHigh");
        addKnob(tone, "Wet HPF", "fxWetHighPass");

        const int space = addGroup("CHORUS / SPACE");
        addKnob(space, "Chorus", "fxChorusMix");
        addKnob(space, "Delay", "fxDelayMix");
        addKnob(space, "Time", "fxDelayTime");
        addKnob(space, "Feedback", "fxDelayFeedback");
        addKnob(space, "Reverb", "fxReverbMix");
        addKnob(space, "Size", "fxReverbSize");

        const int dynamics = addGroup("DYNAMICS");
        addToggle(dynamics, "Compressor", "compEnabled");
        addKnob(dynamics, "Threshold", "compThreshold");
        addKnob(dynamics, "Ratio", "compRatio");
        addKnob(dynamics, "Ceiling", "limiterCeiling");
        addKnob(dynamics, "Master", "masterGain");

        const int quality = addGroup("QUALITY");
        addChoice(quality, "Mode", "qualityMode", { "Draft", "Standard", "High" });
        addKnob(quality, "Poly", "polyphony");
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxPanel)
};