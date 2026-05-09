#pragma once
#include "ParameterPanel.h"

// Performance macro surface - 8 macros, big knobs, always-visible
// regardless of Simple/Advanced mode.
class MacroPanel : public ParameterPanel
{
public:
    explicit MacroPanel(juce::AudioProcessorValueTreeState& apvts)
        : ParameterPanel(apvts, "PERFORMANCE MACROS")
    {
        const int row1 = addGroup("MACROS 1-4");
        addKnob(row1, "Brightness", "macro1");
        addKnob(row1, "Movement",   "macro2");
        addKnob(row1, "Body",       "macro3");
        addKnob(row1, "Air",        "macro4");

        const int row2 = addGroup("MACROS 5-8");
        addKnob(row2, "Shimmer",  "macro5");
        addKnob(row2, "Drive",    "macro6");
        addKnob(row2, "Space",    "macro7");
        addKnob(row2, "Width",    "macro8");
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroPanel)
};
