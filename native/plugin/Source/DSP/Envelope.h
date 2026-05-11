#pragma once
#include <algorithm>

class ADSREnvelope
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    ADSREnvelope() = default;

    void prepare(double sr) { sampleRate = sr; recalculate(); }

    void setAttack(float seconds)  { attackTime = seconds; recalculate(); }
    void setDecay(float seconds)   { decayTime = seconds; recalculate(); }
    void setSustain(float level)   { sustainLevel = std::clamp(level, 0.0f, 1.0f); }
    void setRelease(float seconds) { releaseTime = seconds; recalculate(); }

    void noteOn()
    {
        stage = Stage::Attack;
        currentLevel = 0.0f;
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
                currentLevel += attackRate;
                if (currentLevel >= 1.0f) { currentLevel = 1.0f; stage = Stage::Decay; }
                break;

            case Stage::Decay:
                currentLevel -= decayRate;
                if (currentLevel <= sustainLevel) { currentLevel = sustainLevel; stage = Stage::Sustain; }
                break;

            case Stage::Sustain:
                break;

            case Stage::Release:
                currentLevel -= releaseRate;
                if (currentLevel <= 0.0f) { currentLevel = 0.0f; stage = Stage::Idle; }
                break;
        }

        return currentLevel;
    }

    bool isActive() const { return stage != Stage::Idle; }
    Stage getStage() const { return stage; }

private:
    void recalculate()
    {
        attackRate  = 1.0f / std::max(1.0f, static_cast<float>(sampleRate) * attackTime);
        decayRate   = 1.0f / std::max(1.0f, static_cast<float>(sampleRate) * decayTime);
        releaseRate = 1.0f / std::max(1.0f, static_cast<float>(sampleRate) * releaseTime);
    }

    double sampleRate = 44100.0;
    float attackTime  = 0.01f;
    float decayTime   = 0.3f;
    float sustainLevel = 0.7f;
    float releaseTime = 0.5f;

    float attackRate  = 0.0f;
    float decayRate   = 0.0f;
    float releaseRate = 0.0f;
    float currentLevel = 0.0f;
    Stage stage = Stage::Idle;
};
