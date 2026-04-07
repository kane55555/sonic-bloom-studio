#pragma once
#include <cmath>
#include <algorithm>

class FilterBlock
{
public:
    enum class Type { LP12, LP24, HP12, HP24, BP, Notch };

    FilterBlock() = default;

    void prepare(double sr) { sampleRate = sr; recalculate(); }

    void setCutoff(float freq) { cutoff = std::clamp(freq, 20.0f, 20000.0f); recalculate(); }
    void setResonance(float res) { resonance = std::clamp(res, 0.0f, 1.0f); recalculate(); }
    void setType(Type t) { type = t; recalculate(); }
    void setDrive(float d) { drive = d; }

    float processSample(float input)
    {
        // Apply drive (soft clip)
        if (drive > 0.0f)
            input = std::tanh(input * (1.0f + drive * 4.0f));

        // State variable filter (SVF)
        float q = 1.0f - resonance * 0.9f;
        float f = 2.0f * std::sin(3.14159265f * cutoff / static_cast<float>(sampleRate));

        lowPass += f * bandPass;
        highPass = input - lowPass - q * bandPass;
        bandPass += f * highPass;
        float notch = highPass + lowPass;

        switch (type)
        {
            case Type::LP12: return lowPass;
            case Type::LP24: { secondStageLP += f * secondStageBP; float hp2 = lowPass - secondStageLP - q * secondStageBP; secondStageBP += f * hp2; return secondStageLP; }
            case Type::HP12: return highPass;
            case Type::HP24: return highPass; // Simplified
            case Type::BP:   return bandPass;
            case Type::Notch: return notch;
        }
        return lowPass;
    }

    void reset()
    {
        lowPass = highPass = bandPass = 0.0f;
        secondStageLP = secondStageBP = 0.0f;
    }

private:
    void recalculate() { /* Coefficients updated per-sample in processSample */ }

    double sampleRate = 44100.0;
    float cutoff = 8000.0f;
    float resonance = 0.2f;
    float drive = 0.0f;
    Type type = Type::LP24;

    // SVF state
    float lowPass = 0.0f, highPass = 0.0f, bandPass = 0.0f;
    float secondStageLP = 0.0f, secondStageBP = 0.0f;
};
