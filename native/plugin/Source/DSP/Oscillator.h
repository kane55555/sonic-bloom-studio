#pragma once
#include <cmath>

class Oscillator
{
public:
    enum class Waveform { Sine, Triangle, Saw, Square, SuperSaw, Wavetable };

    Oscillator() = default;

    void prepare(double sampleRate) { this->sampleRate = sampleRate; }

    void setFrequency(float freq) { frequency = freq; }
    void setWaveform(Waveform w) { waveform = w; }
    void setPulseWidth(float pw) { pulseWidth = pw; }
    void setDetune(float cents) { detuneRatio = std::pow(2.0f, cents / 1200.0f); }

    float getNextSample()
    {
        float sample = 0.0f;
        float effectiveFreq = frequency * detuneRatio;
        float phaseIncrement = effectiveFreq / static_cast<float>(sampleRate);

        switch (waveform)
        {
            case Waveform::Sine:
                sample = std::sin(phase * 2.0f * M_PI);
                break;
            case Waveform::Triangle:
                sample = 2.0f * std::abs(2.0f * (phase - std::floor(phase + 0.5f))) - 1.0f;
                break;
            case Waveform::Saw:
                sample = 2.0f * phase - 1.0f;
                break;
            case Waveform::Square:
                sample = (phase < pulseWidth) ? 1.0f : -1.0f;
                break;
            case Waveform::SuperSaw:
                sample = renderSuperSaw();
                break;
            case Waveform::Wavetable:
                sample = std::sin(phase * 2.0f * M_PI); // Placeholder
                break;
        }

        phase += phaseIncrement;
        if (phase >= 1.0f) phase -= 1.0f;

        return sample;
    }

private:
    float renderSuperSaw()
    {
        // 7-voice detuned saw stack
        float sum = 0.0f;
        constexpr int numSaws = 7;
        constexpr float detuneAmounts[numSaws] = {-0.03f, -0.02f, -0.01f, 0.0f, 0.01f, 0.02f, 0.03f};
        for (int i = 0; i < numSaws; ++i)
        {
            float detunedFreq = frequency * (1.0f + detuneAmounts[i] * unisonDetune);
            float p = std::fmod(phase * (detunedFreq / frequency) + static_cast<float>(i) * 0.143f, 1.0f);
            sum += 2.0f * p - 1.0f;
        }
        return sum / numSaws;
    }

    double sampleRate = 44100.0;
    float frequency = 440.0f;
    float phase = 0.0f;
    float pulseWidth = 0.5f;
    float detuneRatio = 1.0f;
    float unisonDetune = 0.3f;
    Waveform waveform = Waveform::Saw;
};
