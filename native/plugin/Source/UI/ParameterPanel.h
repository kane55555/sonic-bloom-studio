#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "KnobLookAndFeel.h"

class ParameterPanel : public juce::Component
{
public:
    ParameterPanel(juce::AudioProcessorValueTreeState& state, juce::String panelTitle = {});
    ~ParameterPanel() override;

    int  addGroup(const juce::String& groupTitle);
    void addKnob  (int groupIndex, const juce::String& name, const juce::String& paramId);
    void addChoice(int groupIndex, const juce::String& name, const juce::String& paramId, const juce::StringArray& items);
    void addToggle(int groupIndex, const juce::String& name, const juce::String& paramId);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    enum class ItemType { Knob, Choice, Toggle };

    struct Item
    {
        juce::Label* label = nullptr;
        juce::Component* component = nullptr;
        ItemType type = ItemType::Knob;
    };

    struct Group
    {
        juce::String title;
        juce::Rectangle<int> bounds {};
        std::vector<Item> items;
        std::vector<std::unique_ptr<juce::Label>> labels;
        std::vector<std::unique_ptr<juce::Slider>> sliders;
        std::vector<std::unique_ptr<juce::ComboBox>> combos;
        std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    };

    bool isValidGroup(int index) const noexcept;
    static std::unique_ptr<juce::Label> makeLabel(const juce::String& text, juce::Justification justification);
    void layoutGroup(Group& group);

    juce::AudioProcessorValueTreeState& apvts;
    juce::String title;
    KnobLookAndFeel knobLAF;
    std::vector<Group> groups;
};
