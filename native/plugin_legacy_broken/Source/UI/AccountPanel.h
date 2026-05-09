#pragma once
#include <JuceHeader.h>
#include "Theme.h"

class AccountPanel : public juce::Component
{
public:
    AccountPanel()
    {
        status.setText("Account system ready for membership login and paid preset banks.", juce::dontSendNotification);
        status.setJustificationType(juce::Justification::centred);
        status.setFont(Theme::getBodyFont(16.0f));
        status.setColour(juce::Label::textColourId, Theme::getColors().textPrimary);
        addAndMakeVisible(status);
    }

    void paint(juce::Graphics& g) override
    {
        const auto& C = Theme::getColors();
        g.fillAll(C.background);
        auto card = getLocalBounds().reduced(80).withSizeKeepingCentre(620, 180);
        g.setColour(C.surface);
        g.fillRoundedRectangle(card.toFloat(), 6.0f);
        g.setColour(C.border);
        g.drawRoundedRectangle(card.toFloat(), 6.0f, 1.0f);
        g.setColour(C.accentPurple);
        g.setFont(Theme::getHeadingFont(18.0f));
        g.drawText("DIDITAGAIN ACCOUNT", card.removeFromTop(46).reduced(18, 0), juce::Justification::centredLeft);
    }

    void resized() override
    {
        status.setBounds(getLocalBounds().reduced(110).withSizeKeepingCentre(560, 80));
    }

private:
    juce::Label status;
};