#pragma once
#include <JuceHeader.h>

struct ThemeColors
{
    juce::Colour background      { 0xff0f1118 };
    juce::Colour surface         { 0xff1a1b2e };
    juce::Colour surfaceElevated { 0xff222336 };
    juce::Colour border          { 0xff2e2f44 };
    juce::Colour textPrimary     { 0xffe0e0ee };
    juce::Colour textSecondary   { 0xff888899 };
    juce::Colour accentPurple    { 0xff8b5cf6 };
    juce::Colour accentTeal      { 0xff2dd4bf };
    juce::Colour silver          { 0xffb0b0c0 };
    juce::Colour destructive     { 0xffef4444 };
    juce::Colour success         { 0xff22c55e };
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
        return juce::Font("Inter", size, juce::Font::bold);
    }

    static juce::Font getBodyFont(float size = 13.0f)
    {
        return juce::Font("Inter", size, juce::Font::plain);
    }

    static juce::Font getMonoFont(float size = 12.0f)
    {
        return juce::Font("JetBrains Mono", size, juce::Font::plain);
    }
};
