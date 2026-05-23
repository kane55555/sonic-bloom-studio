#pragma once
//==============================================================================
//  ChorusBlock.h — BBD/Juno-style stereo chorus.
//
//  Two modulated delay lines per side, with LFOs phase-offset across L/R for
//  a wide, mono-safe stereo image. Wet signal is darkened by a one-pole LP
//  to emulate BBD bandwidth loss. Modes: I (slow, deep, classic), II (faster,
//  shallower, brighter), I+II (both engines summed, the famous wide setting).
//
//  Public API preserves prepare/process/setRate/setDepth/setMix/reset. New:
//      setMode(int 0..2)   — 0=I, 1=II, 2=I+II
//      setCenterMs(float)  — 4..18ms base delay
//==============================================================================
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>

class ChorusBlock
{
public:
    void prepare(double sr, int /*samplesPerBlock*/) noexcept
    {
        sampleRate = sr;
        const int maxSamples = (int) (sampleRate * 0.040); // 40 ms
        for (auto& line : lines) {
            line.assign(maxSamples + 4, 0.0f);
        }
        writeIdx = 0;
        lpL = lpR = 0.0f;
        // ~6 kHz one-pole on the wet path.
        const float fc = 6000.0f;
        lpCoeff = std::exp(-2.0f * juce::MathConstants<float>::pi * fc / (float) sampleRate);
        phase[0] = 0.0f; phase[1] = 0.25f; phase[2] = 0.5f; phase[3] = 0.75f;
    }

    void process(juce::AudioBuffer<float>& buffer) noexcept
    {
        if (mix <= 0.0001f) return;
        const int n = buffer.getNumSamples();
        const int numCh = buffer.getNumChannels();
        auto* L = buffer.getWritePointer(0);
        auto* R = numCh > 1 ? buffer.getWritePointer(1) : L;

        const int lineLen = (int) lines[0].size();
        const float sr = (float) sampleRate;
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float dry = 1.0f - mix;

        // Mode-based parameters.
        const float rate1 = rateHz;
        const float rate2 = rateHz * 2.6f;
        const float center1 = centerMs * 0.001f * sr;
        const float center2 = (centerMs * 0.65f) * 0.001f * sr;
        const float depth1 = depth * 3.5f * 0.001f * sr;  // ms→samples
        const float depth2 = depth * 2.0f * 0.001f * sr;

        for (int i = 0; i < n; ++i)
        {
            const float xL = L[i];
            const float xR = R[i];

            // Write into both delay lines per side (engine I = lines[0/1], II = [2/3])
            lines[0][writeIdx] = xL;
            lines[1][writeIdx] = xR;
            lines[2][writeIdx] = xL;
            lines[3][writeIdx] = xR;

            const float inc1 = rate1 / sr;
            const float inc2 = rate2 / sr;
            phase[0] += inc1; if (phase[0] >= 1.0f) phase[0] -= 1.0f;
            phase[1] += inc1; if (phase[1] >= 1.0f) phase[1] -= 1.0f;
            phase[2] += inc2; if (phase[2] >= 1.0f) phase[2] -= 1.0f;
            phase[3] += inc2; if (phase[3] >= 1.0f) phase[3] -= 1.0f;

            auto modSamples = [&](float ph, float center, float dep) {
                const float s = std::sin(ph * twoPi);
                return center + s * dep;
            };

            auto readLine = [&](const std::vector<float>& line, float delaySamples) {
                float readPos = (float) writeIdx - delaySamples;
                while (readPos < 0.0f) readPos += lineLen;
                while (readPos >= lineLen) readPos -= lineLen;
                const int i0 = (int) readPos;
                const int i1 = (i0 + 1) % lineLen;
                const float frac = readPos - (float) i0;
                return line[i0] + (line[i1] - line[i0]) * frac;
            };

            float wetL = 0.0f, wetR = 0.0f;
            if (mode == 0 || mode == 2) // I
            {
                wetL += readLine(lines[0], modSamples(phase[0], center1, depth1));
                wetR += readLine(lines[1], modSamples(phase[1], center1, depth1));
            }
            if (mode == 1 || mode == 2) // II
            {
                wetL += readLine(lines[2], modSamples(phase[2], center2, depth2));
                wetR += readLine(lines[3], modSamples(phase[3], center2, depth2));
            }
            if (mode == 2) { wetL *= 0.55f; wetR *= 0.55f; }

            // BBD darkness — one-pole LP on wet.
            lpL = wetL + lpCoeff * (lpL - wetL);
            lpR = wetR + lpCoeff * (lpR - wetR);

            L[i] = xL * dry + lpL * mix;
            R[i] = xR * dry + lpR * mix;

            writeIdx = (writeIdx + 1) % lineLen;
        }
    }

    void setRate (float hz)  noexcept { rateHz   = juce::jlimit(0.05f, 6.0f,  hz); }
    void setDepth(float d)   noexcept { depth    = juce::jlimit(0.0f,  1.0f,  d); }
    void setMix  (float m)   noexcept { mix      = juce::jlimit(0.0f,  1.0f,  m); }
    void setMode (int m)     noexcept { mode     = juce::jlimit(0, 2, m); }
    void setCenterMs(float ms) noexcept { centerMs = juce::jlimit(2.0f, 18.0f, ms); }

    void reset() noexcept
    {
        for (auto& line : lines) std::fill(line.begin(), line.end(), 0.0f);
        writeIdx = 0;
        lpL = lpR = 0.0f;
    }

private:
    double sampleRate = 44100.0;
    std::array<std::vector<float>, 4> lines;
    int writeIdx = 0;
    float phase[4] { 0.0f, 0.25f, 0.5f, 0.75f };
    float lpL = 0.0f, lpR = 0.0f;
    float lpCoeff = 0.0f;

    float rateHz   = 0.6f;
    float depth    = 0.5f;
    float mix      = 0.0f;
    float centerMs = 9.0f;
    int   mode     = 0;
};
