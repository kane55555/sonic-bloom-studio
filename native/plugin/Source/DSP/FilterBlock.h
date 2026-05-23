#pragma once
//==============================================================================
//  FilterBlock.h — Multi-mode SVF with pre-filter drive, smoothed cutoff/res
//  and post-filter soft saturation (Juno/Jupiter/CS character).
//
//  Preserves the previous public API; new methods are opt-in:
//      setOutputDrive(float 0..1)  — post-filter tanh saturation
//      setSmoothing(bool)          — turn cutoff/res smoothing on/off
//      setDualMode(bool)           — CS-style HPF -> LPF cascade
//==============================================================================
#include <cmath>
#include <algorithm>

class FilterBlock
{
public:
    enum class Type { LP12, LP24, HP12, HP24, BP, Notch };

    FilterBlock() = default;

    void prepare(double sr)
    {
        sampleRate = sr;
        cutoffSmoothed = cutoff;
        resSmoothed    = resonance;
        // ~5 ms smoother → no zipper noise when knobs sweep.
        const float tauSec = 0.005f;
        smoothCoeff = 1.0f - std::exp(-1.0f / std::max(1.0f, (float) sampleRate * tauSec));
    }

    void setCutoff(float freq)   { cutoff    = std::clamp(freq, 20.0f, 20000.0f); }
    void setResonance(float res) { resonance = std::clamp(res, 0.0f, 1.0f); }
    void setType(Type t)         { type = t; }
    void setDrive(float d)       { drive = std::clamp(d, 0.0f, 1.0f); }
    void setOutputDrive(float d) { outDrive = std::clamp(d, 0.0f, 1.0f); }
    void setSmoothing(bool on)   { smoothingEnabled = on; }
    void setDualMode(bool on)    { dualMode = on; }
    void setHpCutoff(float hz)   { hpCutoff = std::clamp(hz, 20.0f, 4000.0f); }

    float processSample(float input)
    {
        // Smooth cutoff / resonance to avoid zipper noise.
        if (smoothingEnabled)
        {
            cutoffSmoothed += (cutoff    - cutoffSmoothed) * smoothCoeff;
            resSmoothed    += (resonance - resSmoothed)    * smoothCoeff;
        }
        else
        {
            cutoffSmoothed = cutoff;
            resSmoothed    = resonance;
        }

        // Pre-filter drive (Juno-style: warms the input before the ladder).
        if (drive > 0.0f)
            input = std::tanh(input * (1.0f + drive * 4.0f));

        // CS-style dual: pre-HPF into LPF stage.
        if (dualMode)
        {
            const float fHp = 2.0f * std::sin(3.14159265f * hpCutoff / static_cast<float>(sampleRate));
            dualLp += fHp * dualBp;
            const float dualHp = input - dualLp - 0.5f * dualBp;
            dualBp += fHp * dualHp;
            input = dualHp; // HPF output feeds main filter
        }

        const float q = 1.0f - resSmoothed * 0.9f;
        const float f = 2.0f * std::sin(3.14159265f * cutoffSmoothed / static_cast<float>(sampleRate));

        lowPass += f * bandPass;
        highPass = input - lowPass - q * bandPass;
        bandPass += f * highPass;
        const float notch = highPass + lowPass;

        float out = lowPass;
        switch (type)
        {
            case Type::LP12: out = lowPass; break;
            case Type::LP24:
            {
                secondStageLP += f * secondStageBP;
                const float hp2 = lowPass - secondStageLP - q * secondStageBP;
                secondStageBP += f * hp2;
                out = secondStageLP;
                break;
            }
            case Type::HP12: out = highPass; break;
            case Type::HP24: out = highPass; break;
            case Type::BP:   out = bandPass; break;
            case Type::Notch: out = notch;   break;
        }

        // Post-filter soft saturation — keeps resonance peaks musical
        // instead of digital/piercing. Compensated so unity drive is unity.
        if (outDrive > 0.0f)
        {
            const float g = 1.0f + outDrive * 2.5f;
            out = std::tanh(out * g) / std::tanh(g);
        }

        return out;
    }

    void reset()
    {
        lowPass = highPass = bandPass = 0.0f;
        secondStageLP = secondStageBP = 0.0f;
        dualLp = dualBp = 0.0f;
        cutoffSmoothed = cutoff;
        resSmoothed    = resonance;
    }

private:
    double sampleRate = 44100.0;
    float cutoff = 8000.0f;
    float resonance = 0.2f;
    float drive = 0.0f;
    float outDrive = 0.0f;
    bool  smoothingEnabled = true;
    bool  dualMode = false;
    float hpCutoff = 120.0f;
    Type type = Type::LP24;

    float cutoffSmoothed = 8000.0f;
    float resSmoothed    = 0.2f;
    float smoothCoeff    = 0.001f;

    float lowPass = 0.0f, highPass = 0.0f, bandPass = 0.0f;
    float secondStageLP = 0.0f, secondStageBP = 0.0f;
    float dualLp = 0.0f, dualBp = 0.0f;
};
