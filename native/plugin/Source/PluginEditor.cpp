#include "PluginEditor.h"
#include "PluginProcessor.h"

DiditagainStudioV2AudioProcessorEditor::DiditagainStudioV2AudioProcessorEditor (
    DiditagainStudioV2AudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setSize (640, 400);
}

DiditagainStudioV2AudioProcessorEditor::~DiditagainStudioV2AudioProcessorEditor() = default;

void DiditagainStudioV2AudioProcessorEditor::paint (juce::Graphics& g)
{
    // Dark background
    g.fillAll (juce::Colour::fromRGB (18, 18, 22));

    // Subtle top border accent
    g.setColour (juce::Colour::fromRGB (40, 40, 48));
    g.fillRect (0, 0, getWidth(), 1);

    // Title
    g.setColour (juce::Colour::fromRGB (235, 235, 240));
    g.setFont (juce::Font (28.0f, juce::Font::bold));
    g.drawFittedText ("DIDITAGAIN STUDIO",
                      getLocalBounds().reduced (24),
                      juce::Justification::centredTop, 1);

    // Subtitle / phase tag
    g.setColour (juce::Colour::fromRGB (140, 140, 150));
    g.setFont (juce::Font (13.0f));
    g.drawFittedText ("v2 - phase 1",
                      getLocalBounds().reduced (24, 60),
                      juce::Justification::centredTop, 1);
}

void DiditagainStudioV2AudioProcessorEditor::resized() {}
