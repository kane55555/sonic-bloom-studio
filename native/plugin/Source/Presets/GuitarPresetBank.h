#pragma once
//==============================================================================
//  GuitarPresetBank.h
//
//  Hardcoded factory bank of 20 .diapreset definitions that all reference the
//  same source instrument folder (Samples/Guitars/Guitar 1). Seeded to disk
//  the first time the plugin runs so users can immediately browse them.
//==============================================================================
#include "UserPresetFormat.h"

namespace dida { namespace userpreset {

// Returns 20 prebuilt guitar presets pointing at `guitarFolderAbsolutePath`.
juce::Array<UserPreset> buildGuitarBank(const juce::String& guitarFolderAbsolutePath);

}} // namespace dida::userpreset
