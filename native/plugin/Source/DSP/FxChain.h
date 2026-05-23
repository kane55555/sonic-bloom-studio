#pragma once
//==============================================================================
//  FxChain.h — Master post-voice effects chain.
//
//  Order: Saturation -> Chorus -> Wet-HPF -> Delay -> Reverb -> EQ -> Comp
//         -> GainStage (master) -> Limiter (master ceiling)
//
//  Every block is bypassed when its mix/amount is effectively zero so an
//  inactive chain costs almost nothing per sample.
//==============================================================================
#include <JuceHeader.h>
#include "Saturation.h"
#include "ChorusBlock.h"
#include "DelayBlock.h"
#include "ReverbBlock.h"
#include "EQBlock.h"
#include "CompressorBlock.h"
#include "LimiterBlock.h"
#include "GainStage.h"
#include "UtilityDSP.h"

class FxChain
{
public:
    void prepare(double sampleRate, int samplesPerBlock)
    {
        sat.prepare(sampleRate);
        chorus.prepare(sampleRate, samplesPerBlock);
        delay.prepare(sampleRate, samplesPerBlock);
        reverb.prepare(sampleRate, samplesPerBlock);
        eq.prepare(sampleRate, samplesPerBlock);
        comp.prepare(sampleRate, samplesPerBlock);
        limiter.prepare(sampleRate, samplesPerBlock);
        masterGain.prepare(sampleRate, samplesPerBlock);

        juce::dsp::ProcessSpec spec { sampleRate,
                                      static_cast<juce::uint32>(samplesPerBlock), 2 };
        wetHpL.reset(); wetHpR.reset();
        wetHpL.prepare(spec); wetHpR.prepare(spec);
        wetHpL.setType(juce::dsp::FirstOrderTPTFilterType::highpass);
        wetHpR.setType(juce::dsp::FirstOrderTPTFilterType::highpass);
        wetHpL.setCutoffFrequency(wetHpHz);
        wetHpR.setCutoffFrequency(wetHpHz);
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        // 1) Saturation
        if (saturationActive)
        {
            const int n = buffer.getNumSamples();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                auto* d = buffer.getWritePointer(ch);
                for (int i = 0; i < n; ++i) d[i] = sat.processSample(d[i]);
            }
        }

        // 2) Chorus
        chorus.process(buffer);

        // 3) Wet HPF before time-based FX to keep low end clean
        if (wetHpHz > 25.0f && (delayActive || reverbActive))
        {
            const int n = buffer.getNumSamples();
            auto* L = buffer.getWritePointer(0);
            auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : L;
            for (int i = 0; i < n; ++i)
            {
                L[i] = wetHpL.processSample(0, L[i]);
                R[i] = wetHpR.processSample(0, R[i]);
            }
        }

        // 4-5) Time FX
        delay.process(buffer);
        reverb.process(buffer);

        // 6-7) Tone shaping + glue compression
        eq.process(buffer);
        comp.process(buffer);

        // 8) Master gain trim
        masterGain.process(buffer);

        // 9) Final brick-wall safety limiter
        limiter.process(buffer);
    }

    // ---- Setters used by PluginProcessor ----
    void setSaturationDrive(float d) { sat.setDrive(d); saturationActive = d > 0.001f; }
    void setSaturationMix  (float m) { sat.setMix(m);   if (m <= 0.001f) saturationActive = false; }

    void setChorusMix(float m) { chorus.setMix(m); }
    void setChorusRate(float r) { chorus.setRate(r); }
    void setChorusDepth(float d) { chorus.setDepth(d); }

    void setDelayMix(float m) { delay.setMix(m); delayActive = m > 0.001f; }
    void setDelayTime(float s) { delay.setTimeSeconds(s); }
    void setDelayFeedback(float f) { delay.setFeedback(f); }

    void setReverbMix(float m) { reverb.setMix(m); reverbActive = m > 0.001f; }
    void setReverbSize(float s) { reverb.setSize(s); }
    void setReverbDamping(float d) { reverb.setDamping(d); }
    void setReverbWidth(float w) { reverb.setWidth(w); }
    void setReverbCharacter(ReverbBlock::Character c) { reverb.setCharacter(c); }
    void setReverbPreDelayMs(float ms) { reverb.setPreDelayMs(ms); }
    void setReverbDiffusion(float d)   { reverb.setDiffusion(d); }
    void setReverbModRate(float hz)    { reverb.setModRate(hz); }
    void setReverbModDepth(float ms)   { reverb.setModDepth(ms); }
    void setReverbSaturation(float a)  { reverb.setSaturation(a); }
    void setReverbInputHighPassHz(float hz) { reverb.setInputHighPassHz(hz); }
    void setReverbInputHighPassFloorHz(float hz) { reverb.setInputHighPassFloorHz(hz); }
    void setReverbInputLowPassHz(float hz)  { reverb.setInputLowPassHz(hz); }
    void setReverbDucking(float amount, float attackMs = 6.0f, float releaseMs = 260.0f)
    {
        reverb.setDucking(amount, attackMs, releaseMs);
    }
    void setReverbLowMonoControl(float cutoffHz, float lowWidth)
    {
        reverb.setLowMonoControl(cutoffHz, lowWidth);
    }

    void setEqLowDb (float db) { eq.setLowDb(db); }
    void setEqMidDb (float db) { eq.setMidDb(db); }
    void setEqHighDb(float db) { eq.setHighDb(db); }

    void setCompEnabled(bool e)            { comp.setEnabled(e); }
    void setCompThresholdDb(float db)      { comp.setThresholdDb(db); }
    void setCompRatio(float r)             { comp.setRatio(r); }

    void setLimiterCeilingDb(float db)     { limiter.setCeilingDb(db); }
    void setMasterGainDb(float db)         { masterGain.setGainDb(db); }

    void setWetHighPassHz(float hz)
    {
        wetHpHz = juce::jlimit(20.0f, 800.0f, hz);
        wetHpL.setCutoffFrequency(wetHpHz);
        wetHpR.setCutoffFrequency(wetHpHz);
    }

    void reset()
    {
        chorus.reset();
        delay.reset();
        reverb.reset();
        eq.reset();
        comp.reset();
        limiter.reset();
        wetHpL.reset(); wetHpR.reset();
    }

private:
    Saturation       sat;
    ChorusBlock      chorus;
    DelayBlock       delay;
    ReverbBlock      reverb;
    EQBlock          eq;
    CompressorBlock  comp;
    LimiterBlock     limiter;
    GainStage        masterGain;

    juce::dsp::FirstOrderTPTFilter<float> wetHpL, wetHpR;
    float wetHpHz = 80.0f;

    bool saturationActive = false;
    bool delayActive      = false;
    bool reverbActive     = false;
};
