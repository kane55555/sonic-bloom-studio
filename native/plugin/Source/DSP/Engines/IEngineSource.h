#pragma once
//==============================================================================
//  IEngineSource.h — Polymorphic sound-generation interface used by Partials.
//
//  Every synth engine (PCM, Analog, Supersaw, FM, Wavetable, Granular)
//  implements this small interface so a Partial can host any of them
//  interchangeably. Engines render *additively* into a stereo block so the
//  Partial owns the final mix bus, filter and amp envelope.
//==============================================================================
#include <JuceHeader.h>

namespace dida { namespace engines {

// Per-block snapshot of all modulation sources resolved by the Partial.
// Engines read whichever fields they care about; new sources can be added
// without breaking existing engines.
struct ModSnapshot
{
    float velocity   = 1.0f;   // 0..1
    float modWheel   = 0.0f;   // 0..1
    float aftertouch = 0.0f;   // 0..1
    float keytrack   = 0.0f;   // -1..1 around C4
    float env1       = 0.0f;   // 0..1 (amp env)
    float env2       = 0.0f;   // 0..1 (filter/mod env)
    float lfo1       = 0.0f;   // -1..1
    float lfo2       = 0.0f;   // -1..1

    // Voice character (analog drift / jitter) — engines may add to pitch/phase.
    float pitchDriftCents   = 0.0f;
    float cutoffJitterCents = 0.0f;
    float panJitter         = 0.0f;
};

enum class EngineType
{
    Pcm,        // existing multisample playback path
    Analog,     // subtractive: saw/square/pulse/tri/sine/noise + PWM + sub
    Supersaw,   // 3..9 detuned saws
    Fm,         // 4-operator FM
    Wavetable,  // single wavetable osc with morph
    Granular    // grain player placeholder
};

inline EngineType engineTypeFromString(const juce::String& s) noexcept
{
    const auto l = s.toLowerCase();
    if (l == "analog")    return EngineType::Analog;
    if (l == "supersaw")  return EngineType::Supersaw;
    if (l == "fm")        return EngineType::Fm;
    if (l == "wavetable") return EngineType::Wavetable;
    if (l == "granular")  return EngineType::Granular;
    return EngineType::Pcm;
}

inline const char* engineTypeToString(EngineType t) noexcept
{
    switch (t)
    {
        case EngineType::Analog:    return "analog";
        case EngineType::Supersaw:  return "supersaw";
        case EngineType::Fm:        return "fm";
        case EngineType::Wavetable: return "wavetable";
        case EngineType::Granular:  return "granular";
        case EngineType::Pcm:
        default:                    return "pcm";
    }
}

class IEngineSource
{
public:
    virtual ~IEngineSource() = default;

    virtual EngineType type() const noexcept = 0;

    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void reset() = 0;

    virtual void noteOn(int midiNote, float velocity) = 0;
    virtual void noteOff() = 0;

    // Render `numSamples` of stereo audio, ADDING into outL/outR. `pitchHz`
    // is the live (post-glide/drift) fundamental for this block.
    virtual void renderAdd(float* outL, float* outR, int numSamples,
                           float pitchHz, const ModSnapshot& mods) = 0;
};

}} // namespace dida::engines
