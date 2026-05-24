#pragma once
//==============================================================================
//  FmEngine.h — 4-operator phase-modulation engine (DX-style).
//
//  Algorithms (simplified):
//    Stack    : 4 -> 3 -> 2 -> 1   (one carrier, deep modulation)
//    Pair     : (4 -> 3) + (2 -> 1) summed
//    Parallel : 1 + 2 + 3 + 4      (additive, no FM)
//    Bell     : 2 -> 1 and 4 -> 1  (two modulators into one carrier)
//
//  Operator 1 has a feedback loop. Each op has its own ratio, level and
//  4-stage envelope. Output is mono (duplicated to stereo by Partial).
//==============================================================================
#include "IEngineSource.h"
#include <array>

namespace dida { namespace engines {

class FmEngine : public IEngineSource
{
public:
    enum class Algorithm { Stack, Pair, Parallel, Bell };

    struct Op
    {
        float ratio  = 1.0f;
        float level  = 1.0f;   // 0..1
        float attack = 0.005f, decay = 0.3f, sustain = 0.8f, release = 0.4f;
        double phase = 0.0;
        // simple AR-style env state
        float env = 0.0f;
        enum class Stage { Idle, Attack, Decay, Sustain, Release } stage = Stage::Idle;
    };

    EngineType type() const noexcept override { return EngineType::Fm; }

    void prepare(double sr, int) override { sampleRate = sr > 0 ? sr : 44100.0; reset(); }
    void reset() override
    {
        for (auto& o : ops) { o.phase = 0.0; o.env = 0.0f; o.stage = Op::Stage::Idle; }
        feedback = 0.0f;
    }
    void noteOn(int, float vel) override
    {
        velocity = vel;
        for (auto& o : ops) { o.phase = 0.0; o.env = 0.0f; o.stage = Op::Stage::Attack; }
    }
    void noteOff() override
    {
        for (auto& o : ops) o.stage = Op::Stage::Release;
    }

    void setAlgorithm(Algorithm a) noexcept { algo = a; }
    void setFeedbackAmount(float a) noexcept { fbAmount = juce::jlimit(0.0f, 1.0f, a); }
    Op& op(int i) noexcept { return ops[(size_t) juce::jlimit(0, 3, i)]; }

    void renderAdd(float* outL, float* outR, int n, float pitchHz,
                   const ModSnapshot& /*mods*/) override
    {
        const double sr = sampleRate;
        const float vel = juce::jlimit(0.0f, 1.0f, velocity);

        for (int i = 0; i < n; ++i)
        {
            // Update envelopes (per-sample, cheap one-poles)
            for (auto& o : ops) tickEnv(o);

            // Phase increments per op
            std::array<double, 4> inc;
            for (int k = 0; k < 4; ++k) inc[(size_t) k] = (pitchHz * ops[(size_t) k].ratio) / sr;

            float opOut[4] = { 0,0,0,0 };
            auto sine = [](double ph) { return std::sin(ph * juce::MathConstants<double>::twoPi); };

            // op1 has feedback
            const double ph1 = ops[0].phase + (double) feedback * fbAmount;
            opOut[0] = (float) sine(ph1) * ops[0].env * ops[0].level;
            feedback = opOut[0];

            switch (algo)
            {
                case Algorithm::Parallel:
                {
                    for (int k = 1; k < 4; ++k)
                    {
                        opOut[k] = (float) sine(ops[(size_t) k].phase)
                                 * ops[(size_t) k].env * ops[(size_t) k].level;
                    }
                    break;
                }
                case Algorithm::Pair:
                {
                    // 4 -> 3, then 3 carrier; 2 -> 1, 1 carrier
                    opOut[3] = (float) sine(ops[3].phase) * ops[3].env * ops[3].level;
                    opOut[2] = (float) sine(ops[2].phase + opOut[3]) * ops[2].env * ops[2].level;
                    opOut[1] = (float) sine(ops[1].phase) * ops[1].env * ops[1].level;
                    opOut[0] = (float) sine(ops[0].phase + opOut[1] + (double) feedback * fbAmount)
                             * ops[0].env * ops[0].level;
                    feedback = opOut[0];
                    break;
                }
                case Algorithm::Bell:
                {
                    opOut[3] = (float) sine(ops[3].phase) * ops[3].env * ops[3].level;
                    opOut[1] = (float) sine(ops[1].phase) * ops[1].env * ops[1].level;
                    opOut[0] = (float) sine(ops[0].phase + opOut[1] + opOut[3] + (double) feedback * fbAmount)
                             * ops[0].env * ops[0].level;
                    feedback = opOut[0];
                    break;
                }
                case Algorithm::Stack:
                default:
                {
                    opOut[3] = (float) sine(ops[3].phase) * ops[3].env * ops[3].level;
                    opOut[2] = (float) sine(ops[2].phase + opOut[3]) * ops[2].env * ops[2].level;
                    opOut[1] = (float) sine(ops[1].phase + opOut[2]) * ops[1].env * ops[1].level;
                    opOut[0] = (float) sine(ops[0].phase + opOut[1] + (double) feedback * fbAmount)
                             * ops[0].env * ops[0].level;
                    feedback = opOut[0];
                    break;
                }
            }

            // advance phases
            for (int k = 0; k < 4; ++k)
            {
                ops[(size_t) k].phase += inc[(size_t) k];
                if (ops[(size_t) k].phase >= 1.0) ops[(size_t) k].phase -= 1.0;
            }

            float carrier = 0.0f;
            if (algo == Algorithm::Parallel) carrier = 0.25f * (opOut[0] + opOut[1] + opOut[2] + opOut[3]);
            else if (algo == Algorithm::Pair) carrier = 0.5f * (opOut[0] + opOut[2]);
            else                              carrier = opOut[0];

            const float s = carrier * vel;
            outL[i] += s;
            outR[i] += s;
        }
    }

private:
    void tickEnv(Op& o) noexcept
    {
        const float sr = (float) sampleRate;
        auto coeff = [sr](float secs) { return 1.0f - std::exp(-1.0f / juce::jmax(0.0005f, secs) / sr); };
        switch (o.stage)
        {
            case Op::Stage::Attack:
                o.env += coeff(o.attack) * (1.05f - o.env);
                if (o.env >= 1.0f) { o.env = 1.0f; o.stage = Op::Stage::Decay; }
                break;
            case Op::Stage::Decay:
                o.env += coeff(o.decay) * (o.sustain - o.env);
                if (std::fabs(o.env - o.sustain) < 1e-4f) o.stage = Op::Stage::Sustain;
                break;
            case Op::Stage::Sustain:
                o.env = o.sustain;
                break;
            case Op::Stage::Release:
                o.env += coeff(o.release) * (0.0f - o.env);
                if (o.env < 1e-5f) { o.env = 0.0f; o.stage = Op::Stage::Idle; }
                break;
            case Op::Stage::Idle:
                o.env = 0.0f;
                break;
        }
    }

    double sampleRate = 44100.0;
    Algorithm algo = Algorithm::Stack;
    float velocity = 1.0f;
    float feedback = 0.0f;
    float fbAmount = 0.0f;
    std::array<Op, 4> ops {};
};

}} // namespace dida::engines
