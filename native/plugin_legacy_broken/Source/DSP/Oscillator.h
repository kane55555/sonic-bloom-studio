#pragma once
//==============================================================================
//  Oscillator.h — Single-voice oscillator supporting many waveforms,
//  including a per-sample phase offset for FM/PM synthesis.
//
//  All waveforms use a normalised phase in [0, 1). Output is bounded to
//  approximately [-1, 1]. SuperSaw is a 7-voice detuned saw stack.
//==============================================================================
#include <cmath>
#include <algorithm>
#include "UtilityDSP.h"

class Oscillator
{
public:
    enum class Waveform { Sine, Triangle, Saw, Square, Pulse, SuperSaw, FmCarrier, Wavetable };

    Oscillator() = default;

    void prepare(double sr) noexcept { sampleRate = sr; }

    // ---- Setters --------------------------------------------------------
    void setFrequency(float freq)        noexcept { frequency = freq; }
    void setWaveform(Waveform w)         noexcept { waveform = w; }
    void setPulseWidth(float pw)         noexcept { pulseWidth = std::clamp(pw, 0.05f, 0.95f); }
    void setDetuneCents(float cents)     noexcept { detuneRatio = dida::centsToRatio(cents); }
    void setUnisonDetune(float amt)      noexcept { unisonDetune = std::clamp(amt, 0.0f, 1.0f); }
    void setUnisonVoices(int n)          noexcept { unisonVoices = std::clamp(n, 1, 8); }
    void setStereoSpread(float s)        noexcept { stereoSpread = std::clamp(s, 0.0f, 1.0f); }
    void setLevel(float l)               noexcept { level = std::clamp(l, 0.0f, 1.0f); }
    void setPan(float p)                 noexcept { pan = std::clamp(p, -1.0f, 1.0f); }
    void setPhaseOffset(float radians)   noexcept { phaseOffset = radians; }

    // ---- Render --------------------------------------------------------
    float getNextSample() noexcept
    {
        const float effFreq = frequency * detuneRatio;
        const float inc = effFreq / static_cast<float>(sampleRate);

        // Apply per-sample PM offset on top of running phase.
        const float p = phase + phaseOffset * (1.0f / dida::kTwoPi);
        const float wrappedP = p - std::floor(p);

        float sample = 0.0f;
        switch (waveform)
        {
            case Waveform::Sine:
            case Waveform::FmCarrier:
                sample = std::sin(wrappedP * dida::kTwoPi);
                break;
            case Waveform::Triangle:
                sample = 2.0f * std::abs(2.0f * (wrappedP - std::floor(wrappedP + 0.5f))) - 1.0f;
                break;
            case Waveform::Saw:
                sample = 2.0f * wrappedP - 1.0f;
                break;
            case Waveform::Square:
                sample = (wrappedP < 0.5f) ? 1.0f : -1.0f;
                break;
            case Waveform::Pulse:
                sample = (wrappedP < pulseWidth) ? 1.0f : -1.0f;
                break;
            case Waveform::SuperSaw:
                sample = renderSuperSaw();
                break;
            case Waveform::Wavetable:
                // Placeholder — replace with real table lookup later.
                sample = std::sin(wrappedP * dida::kTwoPi)
                       + 0.3f * std::sin(wrappedP * dida::kTwoPi * 2.0f);
                sample *= 0.7f;
                break;
        }

        phase += inc;
        if (phase >= 1.0f) phase -= std::floor(phase);

        return sample;
    }

    void resetPhase(float p = 0.0f) noexcept { phase = p; }

private:
    float renderSuperSaw() noexcept
    {
        // 7-voice detuned saw stack. Detune amount is musical (a few cents).
        constexpr int N = 7;
        constexpr float detune[N] = { -0.030f, -0.020f, -0.010f, 0.0f, 0.010f, 0.020f, 0.030f };
        float sum = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            const float ratio = 1.0f + detune[i] * unisonDetune;
            const float p = std::fmod(phase * ratio + static_cast<float>(i) * 0.143f, 1.0f);
            sum += 2.0f * p - 1.0f;
        }
        return sum * (1.0f / N);
    }

    double sampleRate = 44100.0;
    float  frequency  = 440.0f;
    float  phase      = 0.0f;
    float  phaseOffset = 0.0f;
    float  pulseWidth = 0.5f;
    float  detuneRatio = 1.0f;
    float  unisonDetune = 0.3f;
    int    unisonVoices = 1;
    float  stereoSpread = 0.5f;
    float  level        = 1.0f;
    float  pan          = 0.0f;
    Waveform waveform   = Waveform::Saw;
};
