#pragma once
//==============================================================================
//  VintageSynthBank.h
//
//  20 hardcoded ".diapreset" definitions modelled after classic analog
//  polysynths (Roland Juno/Jupiter, Sequential Prophet, Yamaha CS).
//  All point at Samples/Synths/Lead 1 as a base oscillator source; the
//  vintage character comes from the engine's voice-card + filter + BBD
//  chorus pipeline driven by the macro / velocity / filter-movement blocks.
//==============================================================================
#include "UserPresetFormat.h"

namespace dida { namespace userpreset {

juce::Array<UserPreset> buildVintageSynthBank(const juce::String& synthSourceAbsolutePath);

}} // namespace dida::userpreset
