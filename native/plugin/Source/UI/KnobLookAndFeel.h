#pragma once
//==============================================================================
//  KnobLookAndFeel.h — Custom rotary knob L&F for DIDITAGAIN STUDIO.
//
//  Renders a circular silver knob with a purple/teal arc indicator and a
//  glowing pointer. Designed to scale crisply at any size.
//==============================================================================
#include <JuceHeader.h>
#include "Theme.h"

class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KnobLookAndFeel()
    {
        const auto& c = Theme::getColors();
        setColour(juce::Slider::rotarySliderFillColourId,    c.accentPurple);
        setColour(juce::Slider::rotarySliderOutlineColourId, c.border);
        setColour(juce::Slider::thumbColourId,               c.accentTeal);
        setColour(juce::Slider::textBoxTextColourId,         c.textPrimary);
        setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const auto& C = Theme::getColors();
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(6.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Outer ring (track)
        juce::Path track;
        track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(C.border);
        g.strokePath(track, juce::PathStrokeType(3.0f));

        // Value arc
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                            rotaryStartAngle, angle, true);
        g.setColour(slider.isEnabled() ? C.accentPurple : C.textSecondary);
        g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        // Knob body — radial gradient for a 3D feel
        const float bodyR = radius * 0.78f;
        juce::ColourGradient grad(C.surfaceElevated, centre.x, centre.y - bodyR,
                                  C.surface, centre.x, centre.y + bodyR, false);
        g.setGradientFill(grad);
        g.fillEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour(C.border);
        g.drawEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        // Pointer
        juce::Path p;
        const float pLen = bodyR * 0.88f;
        p.addRectangle(-1.5f, -pLen, 3.0f, pLen * 0.5f);
        p.applyTransform(juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
        g.setColour(C.accentTeal);
        g.fillPath(p);
    }

    juce::Slider::SliderLayout getSliderLayout(juce::Slider& s) override
    {
        // Compact layout that always shows a value box under the knob.
        juce::Slider::SliderLayout layout;
        const auto bounds = s.getLocalBounds();
        layout.textBoxBounds = bounds.removeFromBottom(16);
        layout.sliderBounds  = bounds;
        return layout;
    }

    juce::Label* createSliderTextBox(juce::Slider& s) override
    {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox(s);
        l->setFont(Theme::getMonoFont(11.0f));
        l->setJustificationType(juce::Justification::centred);
        return l;
    }
};
