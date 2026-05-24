#pragma once
//==============================================================================
//  WavetableEngine.h — Single-osc wavetable with frame morph + unison.
//
//  Built-in starter table: 8 frames going Sine -> Triangle -> Saw -> Square
//  with intermediate morphs. The `position` (0..1) selects a frame; the
//  fractional part crossfades into the next frame. Unison stacks up to 5
//  detuned voices for width. `warp` re-maps phase non-linearly for a bend.
//==============================================================================
#include "IEngineSource.h"
#include <array>
#include <random>

namespace dida { namespace engines {

class WavetableEngine : public IEngineSource
{
public:
    static constexpr int kFrames    = 8;
    static constexpr int kFrameSize = 1024;

    EngineType type() const noexcept override { return EngineType::Wavetable; }

    WavetableEngine()
    {
        // Build a simple starter wavetable: morph sine -> tri -> saw -> square.
        for (int f = 0; f < kFrames; ++f)
        {
            const float t = float(f) / float(kFrames - 1); // 0..1
            for (int i = 0; i < kFrameSize; ++i)
            {
                const float p = float(i) / float(kFrameSize);
                const float sine = std::sin(p * juce::MathConstants<float>::twoPi);
                const float tri  = 4.0f * std::fabs(p - 0.5f) - 1.0f;
                const float saw  = 2.0f * p - 1.0f;
                const float sqr  = p < 0.5f ? 1.0f : -1.0f;
                float s;
                if (t < 0.33f)       s = juce::jmap(t / 0.33f, sine, tri);
                else if (t < 0.66f)  s = juce::jmap((t - 0.33f) / 0.33f, tri, saw);
                else                 s = juce::jmap((t - 0.66f) / 0.34f, saw, sqr);
                table[(size_t) f][(size_t) i] = s;
            }
        }
    }

    void prepare(double sr, int) override { sampleRate = sr > 0 ? sr : 44100.0; reset(); }
    void reset() override { for (auto& p : phases) p = 0.0; }
    void noteOn(int, float vel) override
    {
        velocity = vel;
        std::uniform_real_distribution<double> d(0.0, 1.0);
        for (auto& p : phases) p = d(rng);
    }
    void noteOff() override {}

    void setPosition(float p01) noexcept { position = juce::jlimit(0.0f, 1.0f, p01); }
    void setMorphMod(float m)   noexcept { morphMod = juce::jlimit(-1.0f, 1.0f, m); }
    void setUnison(int v)       noexcept { unison = juce::jlimit(1, 5, v); }
    void setDetune(float c)     noexcept { detuneCents = juce::jlimit(0.0f, 30.0f, c); }
    void setWarp(float w)       noexcept { warp = juce::jlimit(-1.0f, 1.0f, w); }

    void renderAdd(float* outL, float* outR, int n, float pitchHz,
                   const ModSnapshot& mods) override
    {
        const double sr = sampleRate;
        const float pos = juce::jlimit(0.0f, 1.0f, position + morphMod * mods.env2);
        const float fIdx = pos * float(kFrames - 1);
        const int   f0   = (int) fIdx;
        const int   f1   = juce::jmin(kFrames - 1, f0 + 1);
        const float fMix = fIdx - float(f0);
        const float vel = juce::jlimit(0.0f, 1.0f, velocity);
        const float inv = 1.0f / float(unison);

        for (int i = 0; i < n; ++i)
        {
            float l = 0.0f, r = 0.0f;
            for (int v = 0; v < unison; ++v)
            {
                const float t = (unison == 1) ? 0.0f : (float(v) / float(unison - 1) - 0.5f) * 2.0f;
                const float cents = t * detuneCents + mods.pitchDriftCents;
                const float fHz = pitchHz * std::pow(2.0f, cents / 1200.0f);
                phases[v] += fHz / sr;
                if (phases[v] >= 1.0) phases[v] -= 1.0;

                // Warp: pull the phase toward 0 or 1 for a bend.
                double ph = phases[v];
                if (warp != 0.0f) ph = std::pow(ph, std::pow(2.0f, warp));

                const float idx = (float) ph * float(kFrameSize);
                const int   i0  = (int) idx & (kFrameSize - 1);
                const int   i1  = (i0 + 1) & (kFrameSize - 1);
                const float mix = idx - std::floor(idx);

                const float a = juce::jmap(mix, table[(size_t) f0][(size_t) i0], table[(size_t) f0][(size_t) i1]);
                const float b = juce::jmap(mix, table[(size_t) f1][(size_t) i0], table[(size_t) f1][(size_t) i1]);
                const float s = juce::jmap(fMix, a, b);

                const float pan = t * 0.5f;
                const float gL = std::cos((pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi);
                const float gR = std::sin((pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi);
                l += s * gL;
                r += s * gR;
            }
            outL[i] += l * inv * vel;
            outR[i] += r * inv * vel;
        }
    }

private:
    std::array<std::array<float, kFrameSize>, kFrames> table {};
    double sampleRate = 44100.0;
    float  position = 0.0f;
    float  morphMod = 0.0f;
    int    unison = 1;
    float  detuneCents = 0.0f;
    float  warp = 0.0f;
    float  velocity = 1.0f;
    std::array<double, 5> phases {};
    std::mt19937 rng { 0xC0FFEEu };
};

}} // namespace dida::engines
