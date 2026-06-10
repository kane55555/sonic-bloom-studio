#pragma once
#include "ParameterPanel.h"

// AI Texture v0.1 — dedicated panel for the cached neural texture layer.
// These controls are a live, global override on top of whatever the loaded
// preset declares in its ai block:
//   * "Texture" toggle  -> aiTextureEnabled  (off fully mutes the texture)
//   * "Amount" knob      -> aiTextureAmount   (0 fully mutes, 1 = preset cap)
// A preset that loaded no neural texture partial is unaffected by these.
class AiTexturePanel : public ParameterPanel
{
public:
    explicit AiTexturePanel(juce::AudioProcessorValueTreeState& apvts)
        : ParameterPanel(apvts, "AI TEXTURE v0.1")
    {
        const int blend = addGroup("NEURAL TEXTURE (CACHED)");
        addToggle(blend, "Texture", "aiTextureEnabled");
        addKnob(blend, "Amount", "aiTextureAmount");
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AiTexturePanel)
};
