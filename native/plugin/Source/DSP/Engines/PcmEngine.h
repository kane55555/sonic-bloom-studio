#pragma once
//==============================================================================
//  PcmEngine.h — Thin adapter exposing the existing multisample/SynthVoice
//  pipeline as the "pcm" engine type.
//
//  This adapter is currently a *marker* engine: it doesn't itself render
//  audio because the host SynthVoice already renders the multisample on its
//  primary path. The adapter exists so the new Partial / engineType plumbing
//  can refer to PCM consistently with Analog/FM/Wavetable/etc. and so
//  presets that opt-in to the new schema can declare `"engineType": "pcm"`
//  without breaking.
//==============================================================================
#include "IEngineSource.h"

namespace dida { namespace engines {

class PcmEngine : public IEngineSource
{
public:
    EngineType type() const noexcept override { return EngineType::Pcm; }
    void prepare(double, int) override {}
    void reset() override {}
    void noteOn(int, float) override {}
    void noteOff() override {}
    void renderAdd(float*, float*, int, float, const ModSnapshot&) override {}
};

}} // namespace dida::engines
