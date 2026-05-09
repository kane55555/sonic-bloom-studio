#pragma once

#include <JuceHeader.h>

class DiditagainStudioV2AudioProcessor;

class DiditagainStudioV2AudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit DiditagainStudioV2AudioProcessorEditor (DiditagainStudioV2AudioProcessor&);
    ~DiditagainStudioV2AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    DiditagainStudioV2AudioProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiditagainStudioV2AudioProcessorEditor)
};
