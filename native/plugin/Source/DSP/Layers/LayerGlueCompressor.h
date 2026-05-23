#pragma once
//==============================================================================
//  LayerGlueCompressor.h — Stereo-linked bus compressor for layer cohesion.
//
//  Soft-knee feedforward design with linked detector (L+R)/2 so the stereo
//  image stays stable. Tuned for "glue": 2:1, ~20ms attack, ~80ms release,
//  1-3 dB of gain reduction on average material with the default threshold.
//==============================================================================
#include <cmath>
#include <algorithm>

class LayerGlueCompressor
{
public:
    void prepare(double sr) noexcept
    {
        sampleRate = sr;
        setAttackMs (attackMs);
        setReleaseMs(releaseMs);
        env = 0.0f;
    }

    void setEnabled    (bool b)   noexcept { enabled = b; }
    void setThresholdDb(float db) noexcept { thresholdDb = db; thresholdLin = std::pow(10.0f, db / 20.0f); }
    void setRatio      (float r)  noexcept { ratio = std::max(1.01f, r); invRatio = 1.0f / ratio; }
    void setMakeupDb   (float db) noexcept { makeup = std::pow(10.0f, db / 20.0f); }
    void setAttackMs(float ms) noexcept
    {
        attackMs = std::max(0.1f, ms);
        attackCoef = std::exp(-1.0f / (0.001f * attackMs * (float) sampleRate));
    }
    void setReleaseMs(float ms) noexcept
    {
        releaseMs = std::max(1.0f, ms);
        releaseCoef = std::exp(-1.0f / (0.001f * releaseMs * (float) sampleRate));
    }

    inline void process(float& l, float& r) noexcept
    {
        if (! enabled) return;
        const float det = 0.5f * (std::fabs(l) + std::fabs(r));
        const float coef = (det > env) ? attackCoef : releaseCoef;
        env = det + (env - det) * coef;

        float gainLin = 1.0f;
        if (env > thresholdLin && env > 1.0e-6f)
        {
            // Soft-knee in linear domain: gr = (env/thr)^(1/ratio - 1)
            const float over = env / thresholdLin;
            gainLin = std::pow(over, invRatio - 1.0f);
        }
        const float g = gainLin * makeup;
        l *= g; r *= g;
    }

private:
    double sampleRate = 44100.0;
    bool   enabled    = true;
    float  thresholdDb = -14.0f;
    float  thresholdLin = 0.1995f;
    float  ratio = 2.0f, invRatio = 0.5f;
    float  attackMs = 20.0f, releaseMs = 80.0f;
    float  attackCoef = 0.0f, releaseCoef = 0.0f;
    float  makeup = 1.122f; // ~+1 dB
    float  env = 0.0f;
};
