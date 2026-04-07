#include "PluginEditor.h"

DiditagainEditor::DiditagainEditor(DiditagainProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(1100, 700);
    setResizable(true, true);
    setResizeLimits(900, 600, 1920, 1080);

    setupTabs();
}

DiditagainEditor::~DiditagainEditor() {}

void DiditagainEditor::setupTabs()
{
    auto setupTab = [this](juce::TextButton& btn, Tab tab) {
        btn.onClick = [this, tab]() { switchTab(tab); };
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e1e2e));
        btn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffe0e0e0));
        addAndMakeVisible(btn);
    };

    setupTab(tabBrowser, Tab::Browser);
    setupTab(tabSynth, Tab::Synth);
    setupTab(tabMod, Tab::Mod);
    setupTab(tabFX, Tab::FX);
    setupTab(tabSettings, Tab::Settings);
    setupTab(tabAccount, Tab::Account);

    // Preset bar
    addAndMakeVisible(presetSelector);
    addAndMakeVisible(prevPreset);
    addAndMakeVisible(nextPreset);
    addAndMakeVisible(savePreset);

    auto& pm = processor.getPresetManager();
    for (int i = 0; i < pm.getNumPresets(); ++i)
        presetSelector.addItem(pm.getPresetName(i), i + 1);
    presetSelector.setSelectedId(pm.getCurrentPresetIndex() + 1, juce::dontSendNotification);

    prevPreset.onClick = [this]() {
        int idx = processor.getPresetManager().getCurrentPresetIndex();
        if (idx > 0) { processor.getPresetManager().loadPreset(idx - 1); presetSelector.setSelectedId(idx, juce::dontSendNotification); }
    };
    nextPreset.onClick = [this]() {
        auto& pm = processor.getPresetManager();
        int idx = pm.getCurrentPresetIndex();
        if (idx < pm.getNumPresets() - 1) { pm.loadPreset(idx + 1); presetSelector.setSelectedId(idx + 2, juce::dontSendNotification); }
    };
}

void DiditagainEditor::switchTab(Tab tab)
{
    currentTab = tab;
    repaint();
}

void DiditagainEditor::paint(juce::Graphics& g)
{
    // Background gradient
    g.fillAll(juce::Colour(0xff0f1118));

    // Header bar
    g.setColour(juce::Colour(0xff181a24));
    g.fillRect(0, 0, getWidth(), 50);

    // Brand
    g.setColour(juce::Colour(0xff8b5cf6));
    g.setFont(juce::Font(18.0f).boldened());
    g.drawText("DIDITAGAIN STUDIO", 15, 0, 250, 50, juce::Justification::centredLeft);

    // Tab indicator
    auto tabBounds = [this](Tab t) -> juce::Rectangle<int> {
        int idx = static_cast<int>(t);
        return { 280 + idx * 90, 15, 80, 22 };
    };

    for (int i = 0; i < 6; ++i)
    {
        Tab t = static_cast<Tab>(i);
        auto bounds = tabBounds(t);
        if (t == currentTab)
        {
            g.setColour(juce::Colour(0xff8b5cf6).withAlpha(0.2f));
            g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
        }
    }

    // Content area placeholder
    g.setColour(juce::Colour(0xff2a2a3a));
    g.fillRoundedRectangle(10.0f, 95.0f, (float)getWidth() - 20.0f, (float)getHeight() - 105.0f, 8.0f);

    g.setColour(juce::Colour(0xff666680));
    g.setFont(14.0f);

    juce::String tabName;
    switch (currentTab) {
        case Tab::Browser: tabName = "Preset Browser"; break;
        case Tab::Synth: tabName = "Synth Engine — Oscillators, Filters, Envelopes"; break;
        case Tab::Mod: tabName = "Modulation Matrix — LFOs, Envelopes, Routings"; break;
        case Tab::FX: tabName = "Effects Chain — Chorus, Delay, Reverb, Distortion"; break;
        case Tab::Settings: tabName = "Settings — Audio, MIDI, Oversampling"; break;
        case Tab::Account: tabName = "Account — License, Subscription, Devices"; break;
    }
    g.drawText(tabName, 0, getHeight() / 2 - 15, getWidth(), 30, juce::Justification::centred);
}

void DiditagainEditor::resized()
{
    auto area = getLocalBounds();

    // Preset bar (below header)
    auto presetBar = area.removeFromTop(50).removeFromTop(0).translated(0, 52);
    int pw = getWidth();
    prevPreset.setBounds(pw - 350, 55, 30, 28);
    presetSelector.setBounds(pw - 315, 55, 200, 28);
    nextPreset.setBounds(pw - 110, 55, 30, 28);
    savePreset.setBounds(pw - 75, 55, 60, 28);

    // Tab buttons
    int tabX = 280;
    for (auto* btn : { &tabBrowser, &tabSynth, &tabMod, &tabFX, &tabSettings, &tabAccount })
    {
        btn->setBounds(tabX, 12, 82, 26);
        tabX += 90;
    }
}
