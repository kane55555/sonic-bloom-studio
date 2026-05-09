#include "ParameterPanel.h"

ParameterPanel::ParameterPanel(juce::AudioProcessorValueTreeState& state, juce::String panelTitle)
    : apvts(state), title(std::move(panelTitle))
{
    setLookAndFeel(&knobLAF);
}

ParameterPanel::~ParameterPanel()
{
    setLookAndFeel(nullptr);
}

bool ParameterPanel::isValidGroup(int index) const noexcept
{
    return index >= 0 && index < static_cast<int>(groups.size());
}

std::unique_ptr<juce::Label> ParameterPanel::makeLabel(const juce::String& text, juce::Justification justification)
{
    auto label = std::make_unique<juce::Label>();
    label->setText(text, juce::dontSendNotification);
    label->setFont(Theme::getBodyFont(10.5f));
    label->setColour(juce::Label::textColourId, Theme::getColors().textSecondary);
    label->setJustificationType(justification);
    return label;
}

int ParameterPanel::addGroup(const juce::String& groupTitle)
{
    Group group;
    group.title = groupTitle;
    groups.push_back(std::move(group));
    return static_cast<int>(groups.size()) - 1;
}

void ParameterPanel::addKnob(int groupIndex, const juce::String& name, const juce::String& paramId)
{
    if (! isValidGroup(groupIndex)) return;
    auto& group = groups[(size_t) groupIndex];

    auto slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                 juce::Slider::TextBoxBelow);
    slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
    slider->setTooltip(name);
    addAndMakeVisible(*slider);

    auto label = makeLabel(name, juce::Justification::centred);
    addAndMakeVisible(*label);

    if (apvts.getParameter(paramId))
        group.sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, paramId, *slider));

    Item item;
    item.label = label.get();
    item.component = slider.get();
    item.type = ItemType::Knob;
    group.items.push_back(item);
    group.labels.push_back(std::move(label));
    group.sliders.push_back(std::move(slider));
}

void ParameterPanel::addChoice(int groupIndex, const juce::String& name, const juce::String& paramId, const juce::StringArray& items)
{
    if (! isValidGroup(groupIndex)) return;
    auto& group = groups[(size_t) groupIndex];

    auto combo = std::make_unique<juce::ComboBox>();
    for (int i = 0; i < items.size(); ++i)
        combo->addItem(items[i], i + 1);
    combo->setTooltip(name);
    combo->setColour(juce::ComboBox::backgroundColourId, Theme::getColors().surfaceElevated);
    combo->setColour(juce::ComboBox::textColourId, Theme::getColors().textPrimary);
    combo->setColour(juce::ComboBox::outlineColourId, Theme::getColors().border);
    addAndMakeVisible(*combo);

    auto label = makeLabel(name, juce::Justification::centredLeft);
    addAndMakeVisible(*label);

    if (apvts.getParameter(paramId))
        group.comboAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, paramId, *combo));

    Item item;
    item.label = label.get();
    item.component = combo.get();
    item.type = ItemType::Choice;
    group.items.push_back(item);
    group.labels.push_back(std::move(label));
    group.combos.push_back(std::move(combo));
}

void ParameterPanel::addToggle(int groupIndex, const juce::String& name, const juce::String& paramId)
{
    if (! isValidGroup(groupIndex)) return;
    auto& group = groups[(size_t) groupIndex];

    auto toggle = std::make_unique<juce::ToggleButton>(name);
    toggle->setColour(juce::ToggleButton::textColourId, Theme::getColors().textPrimary);
    addAndMakeVisible(*toggle);

    if (apvts.getParameter(paramId))
        group.buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, paramId, *toggle));

    Item item;
    item.label = nullptr;
    item.component = toggle.get();
    item.type = ItemType::Toggle;
    group.items.push_back(item);
    group.toggles.push_back(std::move(toggle));
}

void ParameterPanel::paint(juce::Graphics& g)
{
    const auto& C = Theme::getColors();
    g.fillAll(C.background);
    if (title.isNotEmpty())
    {
        g.setColour(C.textPrimary);
        g.setFont(Theme::getHeadingFont(17.0f));
        g.drawText(title, getLocalBounds().withHeight(30).reduced(10, 0), juce::Justification::centredLeft);
    }

    for (auto& group : groups)
    {
        auto b = group.bounds;
        g.setColour(C.surface);
        g.fillRoundedRectangle(b.toFloat(), 6.0f);
        g.setColour(C.border);
        g.drawRoundedRectangle(b.toFloat(), 6.0f, 1.0f);
        g.setColour(C.accentPurple);
        g.setFont(Theme::getBodyFont(12.0f).boldened());
        g.drawText(group.title, b.removeFromTop(24).reduced(10, 2), juce::Justification::centredLeft);
    }
}

void ParameterPanel::resized()
{
    auto area = getLocalBounds().reduced(10);
    if (title.isNotEmpty())
        area.removeFromTop(32);

    const int count = static_cast<int>(groups.size());
    if (count == 0) return;

    const int columns = area.getWidth() >= 1120 ? 3 : (area.getWidth() >= 760 ? 2 : 1);
    const int rows = (count + columns - 1) / columns;
    const int gap = 8;
    const int cellW = (area.getWidth() - gap * (columns - 1)) / columns;
    const int cellH = (area.getHeight() - gap * (rows - 1)) / rows;

    for (int i = 0; i < count; ++i)
    {
        const int col = i % columns;
        const int row = i / columns;
        auto b = juce::Rectangle<int>(area.getX() + col * (cellW + gap),
                                      area.getY() + row * (cellH + gap),
                                      cellW, cellH);
        groups[(size_t) i].bounds = b;
        layoutGroup(groups[(size_t) i]);
    }
}

void ParameterPanel::layoutGroup(Group& group)
{
    auto inner = group.bounds.reduced(10).withTrimmedTop(26);
    const int n = static_cast<int>(group.items.size());
    if (n == 0) return;

    const int columns = juce::jmax(1, inner.getWidth() / 92);
    const int rows = (n + columns - 1) / columns;
    const int cellW = inner.getWidth() / columns;
    const int cellH = inner.getHeight() / rows;

    for (int i = 0; i < n; ++i)
    {
        auto cell = juce::Rectangle<int>(inner.getX() + (i % columns) * cellW,
                                         inner.getY() + (i / columns) * cellH,
                                         cellW, cellH).reduced(4);
        auto& item = group.items[(size_t) i];
        if (item.type == ItemType::Knob)
        {
            if (item.label) item.label->setBounds(cell.removeFromTop(15));
            if (item.component) item.component->setBounds(cell);
        }
        else if (item.type == ItemType::Choice)
        {
            if (item.label) item.label->setBounds(cell.removeFromTop(15));
            if (item.component) item.component->setBounds(cell.removeFromTop(28));
        }
        else
        {
            if (item.component) item.component->setBounds(cell.removeFromTop(28));
        }
    }
}
