#pragma once
//==============================================================================
//  ReverbBlock.h — Premium algorithmic reverb (Schroeder-Moorer-style FDN).
//
//  Signal flow:
//      in -> pre-delay -> 4 diffusion allpass -> 4 modulated combs (with
//      damping LPF + tanh saturation in feedback) -> 2 output allpass ->
//      M/S width -> mix
//
//  Stereo is achieved by running two slightly offset comb sets per channel
//  and a stereo LFO. All allocations happen in prepare(); process() is
//  realtime-safe and branch-light.
//
//  Public API is backwards compatible (setMix/Size/Damping/Width). New
//  setters let presets dial in character per instrument family:
//      Studio, Hall, Dark, Dream, Vintage, Trap, Cathedral, Shimmer.
//==============================================================================
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <algorithm>

class ReverbBlock
{
public:
    enum class Character
    {
        Studio = 0, Hall, Dark, Dream, Vintage, Trap, Cathedral, Shimmer
    };

    void prepare(double sr, int /*samplesPerBlock*/) noexcept
    {
        sampleRate = sr > 1.0 ? sr : 44100.0;
        const double scale = sampleRate / 44100.0;

        // Pre-delay: up to 250ms.
        const int preMax = (int) (0.25 * sampleRate) + 8;
        preBufL.assign((size_t) preMax, 0.0f);
        preBufR.assign((size_t) preMax, 0.0f);
        preIdxL = preIdxR = 0;

        // Schroeder-classic comb sizes scaled to sample rate, stereo-offset.
        const int combSizesL[4] = { 1116, 1188, 1277, 1356 };
        const int combSizesR[4] = { 1139, 1211, 1300, 1379 };
        for (int i = 0; i < 4; ++i)
        {
            combL[i].prepare ((int) std::round(combSizesL[i] * scale));
            combR[i].prepare ((int) std::round(combSizesR[i] * scale));
        }

        const int apSizesL[4] = { 556, 441, 341, 225 };
        const int apSizesR[4] = { 579, 464, 358, 240 };
        for (int i = 0; i < 4; ++i)
        {
            apL[i].prepare ((int) std::round(apSizesL[i] * scale));
            apR[i].prepare ((int) std::round(apSizesR[i] * scale));
        }
        outApL[0].prepare ((int) std::round(556 * scale));
        outApL[1].prepare ((int) std::round(441 * scale));
        outApR[0].prepare ((int) std::round(579 * scale));
        outApR[1].prepare ((int) std::round(464 * scale));

        recompute();
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        if (mix <= 0.0001f) return;
        if (dirty) recompute();

        const int n = buffer.getNumSamples();
        const int nc = buffer.getNumChannels();
        if (n <= 0 || nc <= 0) return;

        auto* L = buffer.getWritePointer(0);
        auto* R = nc > 1 ? buffer.getWritePointer(1) : L;

        const float dry = 1.0f - mix;
        const float wet = mix;
        const float w   = juce::jlimit(0.0f, 1.0f, width);

        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i];
            const float inR = R[i];

            // 1) Pre-delay (independent per channel for subtle stereo).
            preBufL[(size_t) preIdxL] = inL;
            preBufR[(size_t) preIdxR] = inR;
            const int rL = (preIdxL - preSizeL + (int) preBufL.size()) % (int) preBufL.size();
            const int rR = (preIdxR - preSizeR + (int) preBufR.size()) % (int) preBufR.size();
            float xL = preBufL[(size_t) rL];
            float xR = preBufR[(size_t) rR];
            preIdxL = (preIdxL + 1) % (int) preBufL.size();
            preIdxR = (preIdxR + 1) % (int) preBufR.size();

            // 2) Input diffusion (4 series allpass per channel).
            for (auto& a : apL) xL = a.process(xL);
            for (auto& a : apR) xR = a.process(xR);

            // 3) Parallel modulated combs (FDN tank).
            float accL = 0.0f, accR = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                accL += combL[k].process(xL);
                accR += combR[k].process(xR);
            }
            accL *= 0.25f;
            accR *= 0.25f;

            // 4) Output diffusion.
            accL = outApL[0].process(accL);
            accL = outApL[1].process(accL);
            accR = outApR[0].process(accR);
            accR = outApR[1].process(accR);

            // 5) M/S width on wet only.
            const float m = 0.5f * (accL + accR);
            const float s = 0.5f * (accL - accR) * (0.5f + w * 1.5f); // 0.5..2x
            const float wetL = m + s;
            const float wetR = m - s;

            L[i] = inL * dry + wetL * wet;
            R[i] = inR * dry + wetR * wet;
        }
    }

    // ---- Back-compat setters --------------------------------------------------
    void setMix    (float m) noexcept { mix     = juce::jlimit(0.0f, 1.0f, m); }
    void setSize   (float s) noexcept { size    = juce::jlimit(0.0f, 1.0f, s); dirty = true; }
    void setDamping(float d) noexcept { damping = juce::jlimit(0.0f, 1.0f, d); dirty = true; }
    void setWidth  (float w) noexcept { width   = juce::jlimit(0.0f, 1.0f, w); }

    // ---- Premium controls -----------------------------------------------------
    void setPreDelayMs (float ms)  noexcept { preDelayMs = juce::jlimit(0.0f, 200.0f, ms); dirty = true; }
    void setDiffusion  (float d)   noexcept { diffusion  = juce::jlimit(0.0f, 0.92f, d);   dirty = true; }
    void setModRate    (float hz)  noexcept { modRateHz  = juce::jlimit(0.01f, 1.5f, hz);  dirty = true; }
    void setModDepth   (float ms)  noexcept { modDepthMs = juce::jlimit(0.0f, 4.0f, ms);   dirty = true; }
    void setSaturation (float amt) noexcept { satAmount  = juce::jlimit(0.0f, 1.0f, amt);  dirty = true; }

    /** Snap all internal tuning to a named character. Voicing is curated to
        match the spec (Studio/Hall/Dark/Dream/Vintage/Trap/Cathedral/Shimmer). */
    void setCharacter(Character c) noexcept
    {
        character = c;
        switch (c)
        {
            case Character::Studio:    preDelayMs=18; diffusion=0.72f; modRateHz=0.10f; modDepthMs=0.4f; satAmount=0.05f; damping=0.45f; break;
            case Character::Hall:      preDelayMs=30; diffusion=0.78f; modRateHz=0.09f; modDepthMs=0.5f; satAmount=0.06f; damping=0.40f; break;
            case Character::Dark:      preDelayMs=25; diffusion=0.75f; modRateHz=0.06f; modDepthMs=0.4f; satAmount=0.10f; damping=0.78f; break;
            case Character::Dream:     preDelayMs=55; diffusion=0.82f; modRateHz=0.05f; modDepthMs=2.0f; satAmount=0.05f; damping=0.55f; break;
            case Character::Vintage:   preDelayMs=22; diffusion=0.70f; modRateHz=0.08f; modDepthMs=0.6f; satAmount=0.25f; damping=0.65f; break;
            case Character::Trap:      preDelayMs=14; diffusion=0.68f; modRateHz=0.10f; modDepthMs=0.3f; satAmount=0.32f; damping=0.70f; break;
            case Character::Cathedral: preDelayMs=60; diffusion=0.80f; modRateHz=0.04f; modDepthMs=1.0f; satAmount=0.05f; damping=0.35f; break;
            case Character::Shimmer:   preDelayMs=35; diffusion=0.82f; modRateHz=0.18f; modDepthMs=2.5f; satAmount=0.10f; damping=0.30f; break;
        }
        dirty = true;
    }

    void reset() noexcept
    {
        std::fill(preBufL.begin(), preBufL.end(), 0.0f);
        std::fill(preBufR.begin(), preBufR.end(), 0.0f);
        preIdxL = preIdxR = 0;
        for (auto& a : apL)    a.reset();
        for (auto& a : apR)    a.reset();
        for (auto& a : outApL) a.reset();
        for (auto& a : outApR) a.reset();
        for (auto& c : combL)  c.reset();
        for (auto& c : combR)  c.reset();
    }

private:
    struct Allpass
    {
        std::vector<float> buf;
        int idx = 0, size = 1;
        float g = 0.7f;

        void prepare(int sz) { size = std::max(1, sz); buf.assign((size_t) size, 0.0f); idx = 0; }
        void reset()         { std::fill(buf.begin(), buf.end(), 0.0f); idx = 0; }
        float process(float x) noexcept
        {
            const float bufOut = buf[(size_t) idx];
            const float in     = x - bufOut * g;
            buf[(size_t) idx]  = in;
            idx = (idx + 1) % size;
            return bufOut + in * g;
        }
    };

    struct ModComb
    {
        std::vector<float> buf;
        int bufLen = 0;
        int baseSize = 0;
        int writeIdx = 0;

        float damp = 0.5f, dampState = 0.0f;
        float feedback = 0.7f;
        float lfoPhase = 0.0f, lfoInc = 0.0f;
        float modDepthSamples = 0.0f;
        float satAmt = 0.0f;

        void prepare(int sz)
        {
            baseSize = std::max(1, sz);
            // Extra headroom so positive/negative modulation never wraps badly.
            bufLen = baseSize + 32;
            buf.assign((size_t) bufLen, 0.0f);
            writeIdx = 0;
            dampState = 0.0f;
            lfoPhase = 0.0f;
        }
        void reset() { std::fill(buf.begin(), buf.end(), 0.0f); writeIdx = 0; dampState = 0.0f; }

        float process(float in) noexcept
        {
            lfoPhase += lfoInc;
            if (lfoPhase > juce::MathConstants<float>::twoPi)
                lfoPhase -= juce::MathConstants<float>::twoPi;

            const float mod = std::sin(lfoPhase) * modDepthSamples;
            float readPos = (float) writeIdx - (float) baseSize + mod;
            while (readPos < 0.0f)        readPos += (float) bufLen;
            while (readPos >= (float) bufLen) readPos -= (float) bufLen;
            const int   i0   = (int) readPos;
            const float frac = readPos - (float) i0;
            const int   i1   = (i0 + 1) % bufLen;
            const float y    = buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * frac;

            // 1-pole damping in feedback loop.
            dampState = y * (1.0f - damp) + dampState * damp;

            // Tanh saturation in feedback loop for analog warmth.
            float fb = dampState * feedback;
            if (satAmt > 0.0001f)
            {
                const float drive = 1.0f + satAmt * 3.0f;
                fb = std::tanh(fb * drive) * (1.0f / (1.0f + satAmt * 1.2f));
            }

            buf[(size_t) writeIdx] = in + fb;
            writeIdx = (writeIdx + 1) % bufLen;
            return y;
        }
    };

    void recompute() noexcept
    {
        const int pdMax = (int) preBufL.size() - 1;
        const int pd    = juce::jlimit(0, pdMax, (int) (preDelayMs * 0.001 * sampleRate));
        preSizeL = pd;
        preSizeR = juce::jlimit(0, pdMax, pd + (int) (0.002 * sampleRate)); // 2ms offset

        const float diffG  = juce::jlimit(0.0f, 0.92f, diffusion);
        const float outG   = juce::jlimit(0.0f, 0.85f, diffusion * 0.65f);
        for (auto& a : apL)    a.g = diffG;
        for (auto& a : apR)    a.g = diffG;
        for (auto& a : outApL) a.g = outG;
        for (auto& a : outApR) a.g = outG;

        // size 0..1 -> RT60-ish feedback 0.55..0.94
        const float fb = juce::jlimit(0.0f, 0.94f, 0.55f + size * 0.39f);
        // damping 0..1 maps to 1-pole coef toward 1 (more dark)
        const float dampCoef = juce::jlimit(0.0f, 0.93f, damping * 0.93f);

        const float modSamples = (modDepthMs * 0.001f) * (float) sampleRate;
        const float inc        = juce::MathConstants<float>::twoPi * modRateHz / (float) sampleRate;

        for (int i = 0; i < 4; ++i)
        {
            combL[i].feedback = fb;
            combL[i].damp     = dampCoef;
            combL[i].modDepthSamples = modSamples;
            combL[i].lfoInc   = inc * (1.0f + (float) i * 0.013f);
            combL[i].satAmt   = satAmount;

            combR[i].feedback = fb;
            combR[i].damp     = dampCoef;
            combR[i].modDepthSamples = modSamples;
            combR[i].lfoInc   = inc * (1.07f + (float) i * 0.013f); // offset stereo
            combR[i].satAmt   = satAmount;
        }
        dirty = false;
    }

    // --- State ---
    double sampleRate = 44100.0;
    std::vector<float> preBufL, preBufR;
    int preIdxL = 0, preIdxR = 0, preSizeL = 0, preSizeR = 0;

    Allpass apL[4], apR[4];
    Allpass outApL[2], outApR[2];
    ModComb combL[4], combR[4];

    float mix = 0.0f, size = 0.6f, damping = 0.5f, width = 1.0f;
    float diffusion = 0.72f;
    float modRateHz = 0.10f;
    float modDepthMs = 0.5f;
    float satAmount = 0.08f;
    float preDelayMs = 18.0f;
    Character character = Character::Studio;
    bool dirty = true;
};
