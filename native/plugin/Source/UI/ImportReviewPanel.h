#pragma once
#include <JuceHeader.h>
#include "Theme.h"

// Phase 5 placeholder: actual import review happens in the Lovable web admin
// UI (/presets/admin -> Import). This panel surfaces the workflow inside the
// plugin so users know where to go and can re-scan the preset folder.
class ImportReviewPanel : public juce::Component
{
public:
    ImportReviewPanel()
    {
        title.setText("PRESET IMPORT", juce::dontSendNotification);
        title.setFont(Theme::getHeadingFont(17.0f));
        title.setColour(juce::Label::textColourId, Theme::getColors().textPrimary);
        addAndMakeVisible(title);

        body.setMultiLine(true);
        body.setReadOnly(true);
        body.setScrollbarsShown(true);
        body.setCaretVisible(false);
        body.setColour(juce::TextEditor::backgroundColourId, Theme::getColors().surface);
        body.setColour(juce::TextEditor::textColourId,       Theme::getColors().textPrimary);
        body.setColour(juce::TextEditor::outlineColourId,    Theme::getColors().border);
        body.setFont(Theme::getBodyFont(13.0f));
        body.setText(
            "Hybrid Preset Import (v2)\n"
            "\n"
            "1. Drop your raw samples into:\n"
            "     <UserPresets>/_inbox/\n"
            "\n"
            "2. Run the importer:\n"
            "     python native/tools/import_samples.py\n"
            "\n"
            "3. Review and finalize the queue in the web admin:\n"
            "     /presets/admin  ->  Import\n"
            "\n"
            "The importer auto-classifies category, root note, and velocity, then\n"
            "generates a 4-layer hybrid preset (main sample + sine body + air/noise +\n"
            "shimmer) using PresetTemplateFactory defaults per category.\n"
            "\n"
            "Use 'Rescan Presets' below after finalizing to refresh the browser.",
            juce::dontSendNotification);
        addAndMakeVisible(body);

        rescan.setButtonText("RESCAN PRESETS");
        rescan.setColour(juce::TextButton::buttonColourId, Theme::getColors().accentPurple);
        rescan.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(rescan);

        openInbox.setButtonText("OPEN INBOX FOLDER");
        addAndMakeVisible(openInbox);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::getColors().background);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(16);
        title.setBounds(r.removeFromTop(28));
        r.removeFromTop(8);
        auto buttons = r.removeFromBottom(36);
        rescan.setBounds(buttons.removeFromLeft(180).reduced(2));
        buttons.removeFromLeft(8);
        openInbox.setBounds(buttons.removeFromLeft(200).reduced(2));
        r.removeFromBottom(8);
        body.setBounds(r);
    }

    std::function<void()> onRescan;
    std::function<void()> onOpenInbox;

    juce::Label title;
    juce::TextEditor body;
    juce::TextButton rescan;
    juce::TextButton openInbox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImportReviewPanel)
};
