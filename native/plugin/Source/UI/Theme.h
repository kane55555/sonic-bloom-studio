#pragma once
//==============================================================================
//  Theme.h — DIDITAGAIN STUDIO color & font tokens.
//
//  Source of truth for the plugin's visual identity. Match these with the
//  web admin's design system so the brand reads consistently across surfaces.
//==============================================================================
#include <JuceHeader.h>

struct ThemeColors
{
    // Surfaces
    juce::Colour background      { 0xff0B0D10 };
    juce::Colour surface         { 0xff151922 };
    juce::Colour surfaceElevated { 0xff202636 };
    juce::Colour border          { 0xff2A3045 };

    // Text
    juce::Colour textPrimary     { 0xffD7DCE5 }; // silver
    juce::Colour textSecondary   { 0xff7D8596 }; // muted

    // Accents
    juce::Colour accentPurple    { 0xff8B5CF6 };
    juce::Colour accentTeal      { 0xff14F1D9 };
    juce::Colour silver          { 0xffD7DCE5 };

    // Status
    juce::Colour destructive     { 0xffFF4D67 };
    juce::Colour success         { 0xff35F29A };
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
        return juce::Font(juce::Font::getDefaultSansSerifFontName(), size, juce::Font::bold);
    }

    static juce::Font getBodyFont(float size = 13.0f)
    {
        return juce::Font(juce::Font::getDefaultSansSerifFontName(), size, juce::Font::plain);
    }

    static juce::Font getMonoFont(float size = 12.0f)
    {
        return juce::Font(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain);
    }
};
