#pragma once
//==============================================================================
//  OscillatorLayer.h — Optional synth oscillator layer blended on top of a
//  multisample. Provides Saw/Square/Triangle/Noise/Supersaw with a single
//  blend control. Designed to be cheap: one phase + one mix knob.
//==============================================================================
#include <cmath>
#include <random>
#include "UnisonEngine.h"

namespace dida {

class OscillatorLayer
{
public:
    enum class Shape { Saw, Square, Triangle, Noise, Supersaw };

    void prepare(double sr) noexcept
    {
        sampleRate = sr;
        unison.prepare(sr);
        unison.setConfig(5, 0.45f, 0.7f, 0.3f);
    }

    void setShape(Shape s) noexcept { shape = s; }
    void setBlend(float b) noexcept { blend = b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b); }
    void setFrequency(float hz) noexcept { freq = hz; }

    inline void render(float& l, float& r) noexcept
    {
        if (blend <= 0.0001f) return;
        float ol = 0.0f, or_ = 0.0f;
        switch (shape)
        {
            case Shape::Supersaw:
                unison.renderSample(freq, UnisonEngine::Shape::Saw, ol, or_);
                break;
            case Shape::Saw:
            case Shape::Square:
            case Shape::Triangle:
            {
                phase += freq / static_cast<float>(sampleRate);
                if (phase >= 1.0f) phase -= 1.0f;
                float v = 0.0f;
                if (shape == Shape::Saw)      v = 2.0f * (phase - std::floor(phase + 0.5f));
                else if (shape == Shape::Square)   v = (phase < 0.5f) ? 1.0f : -1.0f;
                else /* Triangle */ v = 4.0f * std::abs(phase - std::floor(phase + 0.5f)) - 1.0f;
                ol = or_ = v;
                break;
            }
            case Shape::Noise:
            {
                std::uniform_real_distribution<float> d(-1.0f, 1.0f);
                ol = d(rng); or_ = d(rng);
                break;
            }
        }
        l += ol * blend;
        r += or_ * blend;
    }

private:
    double sampleRate = 44100.0;
    float  freq = 220.0f;
    float  phase = 0.0f;
    float  blend = 0.0f;
    Shape  shape = Shape::Saw;
    UnisonEngine unison;
    std::mt19937 rng { 0xBADCAB1Eu };
};

} // namespace dida
