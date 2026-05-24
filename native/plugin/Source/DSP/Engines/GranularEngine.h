#pragma once
//==============================================================================
//  GranularEngine.h — Placeholder grain player.
//
//  Spec-compliant minimum: grain size, density, position randomisation,
//  pitch spread, stereo spread. Source buffer can be set externally
//  (`setSourceBuffer`). When no source is loaded, emits a soft randomised
//  noise bed so presets are audible at audition time.
//==============================================================================
#include "IEngineSource.h"
#include <array>
#include <random>

namespace dida { namespace engines {

class GranularEngine : public IEngineSource
{
public:
    EngineType type() const noexcept override { return EngineType::Granular; }

    void prepare(double sr, int) override { sampleRate = sr > 0 ? sr : 44100.0; reset(); }
    void reset() override
    {
        for (auto& g : grains) g = {};
        timeUntilNextGrain = 0;
    }
    void noteOn(int midi, float vel) override
    {
        baseMidi = (float) midi;
        velocity = vel;
        timeUntilNextGrain = 0;
    }
    void noteOff() override {}

    void setSourceBuffer(const float* l, const float* r, int numSamples) noexcept
    {
        srcL = l; srcR = r; srcLen = numSamples;
    }

    void setGrainSizeMs(float ms)    noexcept { grainSizeMs = juce::jlimit(5.0f, 500.0f, ms); }
    void setDensityHz(float hz)      noexcept { densityHz   = juce::jlimit(1.0f, 100.0f, hz); }
    void setPositionRand(float r01)  noexcept { positionRand = juce::jlimit(0.0f, 1.0f, r01); }
    void setPitchSpreadSemis(float s) noexcept{ pitchSpread = juce::jlimit(0.0f, 24.0f, s); }
    void setStereoSpread(float a)    noexcept { stereoSpread = juce::jlimit(0.0f, 1.0f, a); }

    void renderAdd(float* outL, float* outR, int n, float pitchHz, const ModSnapshot&) override
    {
        const double sr = sampleRate;
        const float vel = juce::jlimit(0.0f, 1.0f, velocity);
        const int   grainSizeSamples = juce::jmax(8, (int) (grainSizeMs * 0.001f * sr));
        const int   spawnInterval    = juce::jmax(1, (int) (sr / densityHz));
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::uniform_real_distribution<float> p01(0.0f, 1.0f);

        for (int i = 0; i < n; ++i)
        {
            if (timeUntilNextGrain <= 0)
            {
                spawnGrain(grainSizeSamples, pitchHz, u, p01);
                timeUntilNextGrain = spawnInterval;
            }
            --timeUntilNextGrain;

            float l = 0.0f, r = 0.0f;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const float env = hannWindow(float(g.age) / float(g.length));
                float sL, sR;
                readGrainSample(g, sL, sR);
                l += sL * env * g.gainL;
                r += sR * env * g.gainR;
                g.pos += g.step;
                if (++g.age >= g.length) g.active = false;
            }
            outL[i] += l * vel;
            outR[i] += r * vel;
        }
    }

private:
    struct Grain {
        bool   active = false;
        double pos = 0.0;
        double step = 1.0;
        int    age = 0;
        int    length = 0;
        float  gainL = 0.7f, gainR = 0.7f;
    };

    template<class Dist>
    void spawnGrain(int sizeSamples, float pitchHz, Dist& u, Dist& p01_) {
        juce::ignoreUnused(pitchHz);
        for (auto& g : grains)
        {
            if (! g.active)
            {
                g.active = true;
                g.length = sizeSamples;
                g.age    = 0;
                const float midiOffset = u(rng) * pitchSpread;
                g.step   = std::pow(2.0, midiOffset / 12.0);
                if (srcLen > 0)
                {
                    const float r = p01_(rng);
                    g.pos = double(int(r * positionRand * float(srcLen)));
                }
                else
                {
                    g.pos = 0.0;
                }
                const float pan = u(rng) * stereoSpread;
                g.gainL = std::cos((pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi);
                g.gainR = std::sin((pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi);
                return;
            }
        }
    }

    void readGrainSample(const Grain& g, float& l, float& r) const noexcept
    {
        if (srcLen <= 0 || srcL == nullptr)
        {
            // White-noise fallback so the engine is at least audible without a source.
            std::uniform_real_distribution<float> u(-0.1f, 0.1f);
            auto& rngRef = const_cast<std::mt19937&>(rng);
            l = u(rngRef); r = u(rngRef);
            return;
        }
        const int idx = ((int) g.pos) % srcLen;
        l = srcL[idx];
        r = (srcR ? srcR[idx] : l);
    }

    static float hannWindow(float t) noexcept
    {
        t = juce::jlimit(0.0f, 1.0f, t);
        return 0.5f - 0.5f * std::cos(t * juce::MathConstants<float>::twoPi);
    }

    double sampleRate = 44100.0;
    float  grainSizeMs = 80.0f;
    float  densityHz   = 25.0f;
    float  positionRand = 1.0f;
    float  pitchSpread  = 0.0f;
    float  stereoSpread = 0.5f;
    float  velocity = 1.0f;
    float  baseMidi = 60.0f;

    const float* srcL = nullptr;
    const float* srcR = nullptr;
    int          srcLen = 0;

    std::array<Grain, 16> grains {};
    int timeUntilNextGrain = 0;
    std::mt19937 rng { 0xBADA55u };
};

}} // namespace dida::engines
