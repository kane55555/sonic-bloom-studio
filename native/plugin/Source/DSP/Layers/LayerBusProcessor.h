#pragma once
//==============================================================================
//  LayerBusProcessor.h — Shared "glue" bus that all SynthVoice outputs feed
//  BEFORE the global FxChain. This is what turns a stack of independent
//  layers into something that sounds like one premium instrument.
//
//  Signal flow (per-block, stereo):
//
//      voices summed -> shared chorus drift (SharedLFO modulating M/S)
//                    -> shared saturation (tanh glue, parallel mix)
//                    -> glue compression (2:1, ~20ms / ~80ms, link-stereo)
//                    -> stereo widener (subtle M/S)
//                    -> out
//
//  Always-on with conservative defaults so it never colours the sound badly,
//  and tunable per-preset from PluginProcessor if needed in the future.
//==============================================================================
#include <JuceHeader.h>
#include "LayerSaturation.h"
#include "LayerGlueCompressor.h"
#include "LayerStereoProcessor.h"
#include "SharedModulation.h"

class LayerBusProcessor
{
public:
    void prepare(double sr, int /*samplesPerBlock*/)
    {
        sampleRate = sr;
        glue.prepare(sr);
        drift.prepare(sr);
        drift.setRateHz(0.17f);
        drift.setDepth(0.012f);  // ~1.2% stereo drift, ultra subtle
    }

    void reset() noexcept
    {
        glue = LayerGlueCompressor();
        glue.prepare(sampleRate);
    }

    void setEnabled(bool b)            noexcept { enabled = b; }
    void setSaturationDrive(float d)   noexcept { sat.setDrive(d); }
    void setSaturationMix  (float m)   noexcept { sat.setMix(m); }
    void setCompEnabled(bool b)        noexcept { glue.setEnabled(b); }
    void setCompThresholdDb(float db)  noexcept { glue.setThresholdDb(db); }
    void setCompRatio(float r)         noexcept { glue.setRatio(r); }
    void setWidth(float w)             noexcept { widener.setWidth(w); }
    void setDriftRate (float hz)       noexcept { drift.setRateHz(hz); }
    void setDriftDepth(float d)        noexcept { drift.setDepth(d); }

    void process(juce::AudioBuffer<float>& buffer)
    {
        if (! enabled) return;
        const int n = buffer.getNumSamples();
        if (n <= 0) return;
        auto* L = buffer.getWritePointer(0);
        auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : L;

        for (int i = 0; i < n; ++i)
        {
            float l = L[i];
            float r = R[i];

            // 1) Shared subtle stereo drift — moves layers TOGETHER
            const float driftMod = drift.tick();
            // apply as opposite-sign micro gain to L/R (very small)
            l *= (1.0f + driftMod);
            r *= (1.0f - driftMod);

            // 2) Shared saturation (parallel)
            l = sat.process(l);
            r = sat.process(r);

            // 3) Glue compression (stereo-linked)
            glue.process(l, r);

            // 4) Stereo widener
            widener.process(l, r);

            L[i] = l;
            if (R != L) R[i] = r;
        }
    }

private:
    double sampleRate = 44100.0;
    bool   enabled = true;

    LayerSaturation       sat;
    LayerGlueCompressor   glue;
    LayerStereoProcessor  widener;
    SharedLFO             drift;
};
