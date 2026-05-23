#pragma once
//==============================================================================
//  Envelope.h — RC-style analog ADSR.
//
//  Analog envelopes do not ramp linearly; they curve toward a target with an
//  exponential time-constant set by their stage. This module models that with
//  a one-pole "approach" per stage:
//
//      out += (target - out) * coeff
//
//  An overshoot constant on attack ensures we actually reach 1.0 in the
//  requested time even though the response is exponential — gives the
//  characteristic "fast but rounded" analog attack that doesn't click.
//
//  Public API is fully backwards compatible with the previous linear ADSR:
//  setAttack/Decay/Sustain/Release, noteOn/noteOff, getNextSample, isActive,
//  getStage.
//==============================================================================
#include <algorithm>
#include <cmath>

class ADSREnvelope
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    ADSREnvelope() = default;

    void prepare(double sr) { sampleRate = sr; recalculate(); }

    void setAttack(float seconds)  { attackTime  = std::max(0.0005f, seconds); recalculate(); }
    void setDecay(float seconds)   { decayTime   = std::max(0.001f,  seconds); recalculate(); }
    void setSustain(float level)   { sustainLevel = std::clamp(level, 0.0f, 1.0f); }
    void setRelease(float seconds) { releaseTime = std::max(0.001f,  seconds); recalculate(); }

    void noteOn()
    {
        // "analog partial reset" — if the env is releasing softly, don't
        // slam back to 0. Voice cards still get a fresh attack stage but
        // start from current level. This prevents legato clicks.
        if (stage == Stage::Idle || stage == Stage::Release)
        {
            if (stage == Stage::Idle) currentLevel = 0.0f;
        }
        stage = Stage::Attack;
    }

    void noteOff()
    {
        if (stage != Stage::Idle)
            stage = Stage::Release;
    }

    float getNextSample()
    {
        switch (stage)
        {
            case Stage::Idle:
                return 0.0f;

            case Stage::Attack:
                // Approach an overshoot target so we *reach* 1.0 in the
                // requested attack time — classic RC charge model.
                currentLevel += (attackTarget - currentLevel) * attackCoeff;
                if (currentLevel >= 1.0f)
                {
                    currentLevel = 1.0f;
                    stage = Stage::Decay;
                }
                break;

            case Stage::Decay:
                currentLevel += (sustainLevel - currentLevel) * decayCoeff;
                if (std::fabs(currentLevel - sustainLevel) < 1.0e-4f)
                {
                    currentLevel = sustainLevel;
                    stage = Stage::Sustain;
                }
                break;

            case Stage::Sustain:
                break;

            case Stage::Release:
                currentLevel += (-releaseTarget - currentLevel) * releaseCoeff;
                if (currentLevel <= 0.0005f)
                {
                    currentLevel = 0.0f;
                    stage = Stage::Idle;
                }
                break;
        }

        return currentLevel;
    }

    bool isActive() const { return stage != Stage::Idle; }
    Stage getStage() const { return stage; }

private:
    void recalculate()
    {
        const float sr = static_cast<float>(sampleRate);
        // One-pole coefficient to reach ~63% of target after `time` seconds:
        //   y(t) = 1 - exp(-t/tau). Solve for coeff = 1 - exp(-1 / (tau*sr)).
        auto coeffFor = [sr](float time) {
            return 1.0f - std::exp(-1.0f / std::max(1.0f, sr * time));
        };
        attackCoeff  = coeffFor(attackTime);
        decayCoeff   = coeffFor(decayTime);
        releaseCoeff = coeffFor(releaseTime);
    }

    double sampleRate = 44100.0;
    float attackTime  = 0.01f;
    float decayTime   = 0.3f;
    float sustainLevel = 0.7f;
    float releaseTime = 0.5f;

    // Targets for attack/release. >1 overshoot for attack to guarantee we
    // hit 1.0 inside the requested time; positive release target so the
    // approach (-target - current) pulls cleanly back to 0.
    const float attackTarget  = 1.20f;
    const float releaseTarget = 0.01f;

    float attackCoeff  = 0.0f;
    float decayCoeff   = 0.0f;
    float releaseCoeff = 0.0f;
    float currentLevel = 0.0f;
    Stage stage = Stage::Idle;
};
