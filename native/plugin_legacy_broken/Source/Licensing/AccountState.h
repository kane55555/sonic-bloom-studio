#pragma once
#include <JuceHeader.h>

struct AccountState
{
    bool loggedIn = false;
    juce::String email;
    juce::String displayName;
    juce::String plan;
    juce::String subscriptionStatus; // "active", "past_due", "canceled"
    int maxDevices = 2;
    int activeDevices = 0;
    juce::StringArray ownedPacks;
};
