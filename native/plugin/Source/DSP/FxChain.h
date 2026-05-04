#pragma once
//==============================================================================
//  FxChain.h — Master post-voice effects chain.
//
//  Order: Saturation -> Chorus -> Delay -> Reverb -> safety limiter.
//  Every block is bypassed when its mix/amount is effectively zero so an
//  inactive chain costs almost nothing per sample.
//==============================================================================
#include <JuceHeader.h>
#include "Saturation.h"
#include "ChorusBlock.h"
#include "DelayBlock.h"
#include "ReverbBlock.h"
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
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        // 1) Saturation (per-sample)
        if (saturationActive)
        {
            const int n = buffer.getNumSamples();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                auto* d = buffer.getWritePointer(ch);
                for (int i = 0; i < n; ++i) d[i] = sat.processSample(d[i]);
            }
        }

        // 2) Chorus / 3) Delay / 4) Reverb (block based, internally bypass on 0 mix)
        chorus.process(buffer);
        delay.process(buffer);
        reverb.process(buffer);

        // 5) Safety limiter — soft clip the master bus so we never deliver
        //    > 0 dBFS even with extreme preset settings.
        const int n = buffer.getNumSamples();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* d = buffer.getWritePointer(ch);
            for (int i = 0; i < n; ++i) d[i] = dida::softClip(d[i]);
        }
    }

    // ---- Setters used by PluginProcessor ----
    void setSaturationDrive(float d) { sat.setDrive(d); saturationActive = d > 0.001f; }
    void setSaturationMix  (float m) { sat.setMix(m);   if (m <= 0.001f) saturationActive = false; }

    void setChorusMix(float m) { chorus.setMix(m); }
    void setChorusRate(float r) { chorus.setRate(r); }
    void setChorusDepth(float d) { chorus.setDepth(d); }

    void setDelayMix(float m) { delay.setMix(m); }
    void setDelayTime(float s) { delay.setTimeSeconds(s); }
    void setDelayFeedback(float f) { delay.setFeedback(f); }

    void setReverbMix(float m) { reverb.setMix(m); }
    void setReverbSize(float s) { reverb.setSize(s); }
    void setReverbDamping(float d) { reverb.setDamping(d); }

    void reset()
    {
        chorus.reset();
        delay.reset();
        reverb.reset();
    }

private:
    Saturation   sat;
    ChorusBlock  chorus;
    DelayBlock   delay;
    ReverbBlock  reverb;
    bool         saturationActive = false;
};
