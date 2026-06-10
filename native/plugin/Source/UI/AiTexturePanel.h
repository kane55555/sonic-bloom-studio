#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "KnobLookAndFeel.h"
#include "../PluginProcessor.h"

// AI Texture v0.2 — import + freeze panel for the cached neural texture layer.
//
// Live, global overrides on top of whatever the loaded preset declares:
//   * "Texture" toggle  -> aiTextureEnabled  (off fully mutes the texture)
//   * "Amount" knob      -> aiTextureAmount   (0 fully mutes, 1 = preset cap)
//   * "Solo (debug)"     -> transient processor flag (never saved to a preset)
//
// Plus message-thread file operations routed through PresetManager:
//   Import Texture WAV / Remove Texture / Open Texture Folder / Freeze Texture.
// A preset that loaded no neural texture partial is unaffected by these.
class AiTexturePanel : public juce::Component,
                       private juce::Timer
{
public:
    explicit AiTexturePanel(DiditagainProcessor& proc)
        : processor(proc)
    {
        setLookAndFeel(&knobLAF);
        auto& apvts = processor.getAPVTS();
        auto& C = Theme::getColors();

        enabledToggle.setButtonText("Texture");
        enabledToggle.setColour(juce::ToggleButton::textColourId, C.textPrimary);
        addAndMakeVisible(enabledToggle);
        if (apvts.getParameter("aiTextureEnabled"))
            enabledAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                apvts, "aiTextureEnabled", enabledToggle);

        amount.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        amount.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
        addAndMakeVisible(amount);
        if (apvts.getParameter("aiTextureAmount"))
            amountAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                apvts, "aiTextureAmount", amount);
        amountLabel.setText("Amount", juce::dontSendNotification);
        amountLabel.setJustificationType(juce::Justification::centred);
        amountLabel.setColour(juce::Label::textColourId, C.textSecondary);
        addAndMakeVisible(amountLabel);

        soloToggle.setButtonText("Solo AI Texture (debug)");
        soloToggle.setColour(juce::ToggleButton::textColourId, C.textPrimary);
        soloToggle.setToggleState(processor.getAiTextureSolo(), juce::dontSendNotification);
        soloToggle.onClick = [this] { processor.setAiTextureSolo(soloToggle.getToggleState()); };
        addAndMakeVisible(soloToggle);

        auto setupButton = [this, &C](juce::TextButton& b, const juce::String& text)
        {
            b.setButtonText(text);
            b.setColour(juce::TextButton::buttonColourId, C.surfaceElevated);
            b.setColour(juce::TextButton::textColourOffId, C.textPrimary);
            addAndMakeVisible(b);
        };
        setupButton(importButton, "Import Texture WAV");
        setupButton(removeButton, "Remove Texture");
        setupButton(openFolderButton, "Open Texture Folder");
        setupButton(freezeButton, "Freeze Texture to Preset");
        setupButton(installPackButton, "Install Pack ZIP");

        importButton.onClick      = [this] { doImport(); };
        installPackButton.onClick = [this] { doInstallPack(); };
        removeButton.onClick      = [this] { showResult(processor.getPresetManager().aiRemoveTexture()); };
        freezeButton.onClick      = [this] { showResult(processor.getPresetManager().aiFreezeTexture()); };
        openFolderButton.onClick  = [this]
        {
            auto folder = processor.getPresetManager().aiNeuralTextureFolder();
            folder.createDirectory();
            folder.revealToUser();
        };

        statusLabel.setJustificationType(juce::Justification::centredLeft);
        statusLabel.setColour(juce::Label::textColourId, C.textPrimary);
        addAndMakeVisible(statusLabel);

        messageLabel.setJustificationType(juce::Justification::centredLeft);
        messageLabel.setColour(juce::Label::textColourId, C.textSecondary);
        messageLabel.setFont(Theme::getBodyFont(11.0f));
        addAndMakeVisible(messageLabel);

        refreshStatus();
        startTimerHz(3);
    }

    ~AiTexturePanel() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        const auto& C = Theme::getColors();
        g.fillAll(C.background);
        g.setColour(C.textPrimary);
        g.setFont(Theme::getHeadingFont(17.0f));
        g.drawText("AI TEXTURE v0.2", getLocalBounds().withHeight(30).reduced(10, 0),
                   juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        area.removeFromTop(34);

        auto top = area.removeFromTop(96);
        enabledToggle.setBounds(top.removeFromLeft(110).withTrimmedTop(36).withHeight(28));
        auto knobCell = top.removeFromLeft(96);
        amountLabel.setBounds(knobCell.removeFromTop(16));
        amount.setBounds(knobCell);

        area.removeFromTop(8);
        soloToggle.setBounds(area.removeFromTop(28));
        area.removeFromTop(8);

        const int bh = 30, gap = 6;
        importButton.setBounds(area.removeFromTop(bh));      area.removeFromTop(gap);
        installPackButton.setBounds(area.removeFromTop(bh));  area.removeFromTop(gap);
        freezeButton.setBounds(area.removeFromTop(bh));       area.removeFromTop(gap);
        removeButton.setBounds(area.removeFromTop(bh));       area.removeFromTop(gap);
        openFolderButton.setBounds(area.removeFromTop(bh));   area.removeFromTop(gap);

        area.removeFromTop(8);
        statusLabel.setBounds(area.removeFromTop(22));
        messageLabel.setBounds(area.removeFromTop(40));
    }

private:
    void timerCallback() override { refreshStatus(); }

    void refreshStatus()
    {
        const auto s = processor.getPresetManager().aiTextureStatus();
        statusLabel.setText("Status: " + s, juce::dontSendNotification);
        soloToggle.setToggleState(processor.getAiTextureSolo(), juce::dontSendNotification);
    }

    void doImport()
    {
        chooser = std::make_unique<juce::FileChooser>(
            "Select a cached neural texture WAV", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File()) return;
                showResult(processor.getPresetManager().aiImportTextureWav(file));
            });
    }

    void doInstallPack()
    {
        chooser = std::make_unique<juce::FileChooser>(
            "Select a DIDITAGAIN preset/audio pack ZIP", juce::File(), "*.zip");
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File()) return;
                showResult(processor.getPresetManager().installPresetPackFromZip(file));
            });

    void showResult(const PresetManager::AiTextureOpResult& r)
    {
        messageLabel.setText(r.message, juce::dontSendNotification);
        refreshStatus();
    }

    DiditagainProcessor& processor;
    KnobLookAndFeel knobLAF;

    juce::ToggleButton enabledToggle;
    juce::Slider       amount;
    juce::Label        amountLabel;
    juce::ToggleButton soloToggle;
    juce::TextButton   importButton, removeButton, openFolderButton, freezeButton;
    juce::Label        statusLabel, messageLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttach;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AiTexturePanel)
};
