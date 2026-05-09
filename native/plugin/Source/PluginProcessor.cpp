#include "PluginProcessor.h"
#include "PluginEditor.h"

DiditagainStudioV2AudioProcessor::DiditagainStudioV2AudioProcessor()
    : AudioProcessor (BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

DiditagainStudioV2AudioProcessor::~DiditagainStudioV2AudioProcessor() = default;

void DiditagainStudioV2AudioProcessor::prepareToPlay (double, int) {}
void DiditagainStudioV2AudioProcessor::releaseResources() {}

bool DiditagainStudioV2AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

void DiditagainStudioV2AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                     juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

juce::AudioProcessorEditor* DiditagainStudioV2AudioProcessor::createEditor()
{
    return new DiditagainStudioV2AudioProcessorEditor (*this);
}

void DiditagainStudioV2AudioProcessor::getStateInformation (juce::MemoryBlock&) {}
void DiditagainStudioV2AudioProcessor::setStateInformation (const void*, int) {}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DiditagainStudioV2AudioProcessor();
}
