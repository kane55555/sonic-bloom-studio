#pragma once
#include <cmath>

class LFO
{
public:
    enum class Shape { Sine, Triangle, Saw, Square, SampleAndHold };

    LFO() = default;

    void prepare(double sr) { sampleRate = sr; }
    void setRate(float hz) { rate = hz; }
    void setShape(Shape s) { shape = s; }
    void setSync(bool enabled) { synced = enabled; }
    void reset() { phase = 0.0f; }

    float getNextSample()
    {
        constexpr float twoPi = 6.28318530717958647692f;
        float sample = 0.0f;

        switch (shape)
        {
            case Shape::Sine:
                sample = std::sin(phase * twoPi);
                break;
            case Shape::Triangle:
                sample = 2.0f * std::abs(2.0f * (phase - std::floor(phase + 0.5f))) - 1.0f;
                break;
            case Shape::Saw:
                sample = 2.0f * phase - 1.0f;
                break;
            case Shape::Square:
                sample = (phase < 0.5f) ? 1.0f : -1.0f;
                break;
            case Shape::SampleAndHold:
                if (phase < lastPhase) // phase wrapped
                    shValue = (static_cast<float>(std::rand()) / RAND_MAX) * 2.0f - 1.0f;
                sample = shValue;
                break;
        }

        lastPhase = phase;
        phase += rate / static_cast<float>(sampleRate);
        if (phase >= 1.0f) phase -= 1.0f;

        return sample;
    }

private:
    double sampleRate = 44100.0;
    float rate = 1.0f;
    float phase = 0.0f;
    float lastPhase = 0.0f;
    float shValue = 0.0f;
    Shape shape = Shape::Sine;
    bool synced = false;
};
