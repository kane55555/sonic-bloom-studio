#pragma once
//==============================================================================
//  Theme.h — DIDITAGAIN STUDIO color & font tokens.
//
//  Premium dark + teal identity. The "accentPurple" field name is kept for
//  source compat with existing call sites, but the colour is now teal so all
//  panels pick up the new look automatically.
//==============================================================================
#include <JuceHeader.h>

struct ThemeColors
{
    // Surfaces — charcoal / graphite
    juce::Colour background      { 0xff0A0C10 };
    juce::Colour surface         { 0xff12161D };
    juce::Colour surfaceElevated { 0xff1A2028 };
    juce::Colour border          { 0xff232A36 };

    // Text
    juce::Colour textPrimary     { 0xffD7DCE5 };
    juce::Colour textSecondary   { 0xff7D8596 };

    // Accents — teal forward
    juce::Colour accentPurple    { 0xff14F1D9 }; // legacy name, now teal
    juce::Colour accentTeal      { 0xff14F1D9 };
    juce::Colour accentTealDim   { 0xff0EAFA0 };
    juce::Colour silver          { 0xffD7DCE5 };

    juce::Colour destructive     { 0xffFF4D67 };
    juce::Colour success         { 0xff14F1D9 };
};

class Theme
{
public:
    static const ThemeColors& getColors()
    {
        static ThemeColors colors;
        return colors;
    }

    static juce::Font getHeadingFont(float size = 18.0f)
    {
        return juce::Font(juce::FontOptions(juce::Font::getDefaultSansSerifFontName(), size, juce::Font::bold));
    }

    static juce::Font getBodyFont(float size = 13.0f)
    {
        return juce::Font(juce::FontOptions(juce::Font::getDefaultSansSerifFontName(), size, juce::Font::plain));
    }

    static juce::Font getMonoFont(float size = 12.0f)
    {
        return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
    }
};
