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
        // 0) FX input metering — catches presets that hand the chain a hot
        //    signal so the cause is visible in logs before the limiter kicks.
        meterStage(buffer, "fx-in", fxInPeak, fxInFrames);

        // Capture pre-FX peak for the quality reporter.
        captureRecentPeak(buffer, fxInRecent);

        // 0.5) Update note-density tracker from the dry buffer envelope and
        //      compute the send multiplier shared by delay + reverb. This is
        //      what stops scale runs and fast chords from turning into a
        //      muddy reverb/delay cloud.
        updateNoteDensity(buffer);
        const float densityReduction = noteDensityFxReductionEnabled
            ? juce::jlimit(0.0f, maxDensityReduction, densityEnv * maxDensityReduction)
            : 0.0f;
        const float choirDelayScale  = (choirDensityMode && activeVoiceCountForDensity >= 8) ? 0.50f : 1.0f;
        const float choirReverbScale = (choirDensityMode && activeVoiceCountForDensity >= 8) ? 0.65f : 1.0f;
        delay.setSendDensityScale (choirDelayScale  * (1.0f - densityReduction * delayDensityWeight));
        reverb.setSendDensityScale(choirReverbScale * (1.0f - densityReduction * reverbDensityWeight));

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

        // 8.5) Clip-warning instrumentation — log if any sample crosses the
        //      brick-wall ceiling BEFORE the limiter saves us. Rate-limited
        //      to 1 message per ~500ms so a runaway preset can't flood logs.
        detectAndLogClipping(buffer);

        // 8.6) Capture pre-limiter "fx out" peak for the quality reporter.
        captureRecentPeak(buffer, fxOutRecent);

        // 9) Final brick-wall safety limiter
        limiter.process(buffer);

        // 9.5) Capture post-limiter "final" peak for the quality reporter.
        captureRecentPeak(buffer, finalRecent);
    }

    // Process only the separated per-voice FX-send bus and leave no dry
    // pass-through in the returned buffer. SynthEngine adds this wet result
    // back to the independent dry voice bus.
    void processWetSend(juce::AudioBuffer<float>& buffer)
    {
        meterStage(buffer, "fx-send-in", fxInPeak, fxInFrames);
        captureRecentPeak(buffer, fxInRecent);
        updateNoteDensity(buffer);

        const float densityReduction = noteDensityFxReductionEnabled
            ? juce::jlimit(0.0f, maxDensityReduction, densityEnv * maxDensityReduction)
            : 0.0f;
        const float choirDelayScale  = (choirDensityMode && activeVoiceCountForDensity >= 8) ? 0.50f : 1.0f;
        const float choirReverbScale = (choirDensityMode && activeVoiceCountForDensity >= 8) ? 0.65f : 1.0f;
        delay.setSendDensityScale (choirDelayScale  * (1.0f - densityReduction * delayDensityWeight));
        reverb.setSendDensityScale(choirReverbScale * (1.0f - densityReduction * reverbDensityWeight));

        if (saturationActive)
        {
            const int n = buffer.getNumSamples();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                auto* d = buffer.getWritePointer(ch);
                for (int i = 0; i < n; ++i) d[i] = sat.processSample(d[i]);
            }
        }

        chorus.process(buffer);

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

        dryFxScratch.setSize(buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
        reverbWetScratch.setSize(buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
        dryFxScratch.makeCopyOf(buffer, true);

        delay.processWetOnly(buffer, dryFxScratch);
        reverbWetScratch.makeCopyOf(dryFxScratch, true);
        reverb.processWetOnly(reverbWetScratch, dryFxScratch);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom(ch, 0, reverbWetScratch, ch, 0, buffer.getNumSamples());

        detectAndLogClipping(buffer);
        captureRecentPeak(buffer, fxOutRecent);
    }

    void finalizeOutput(juce::AudioBuffer<float>& buffer)
    {
        eq.process(buffer);
        comp.process(buffer);
        masterGain.process(buffer);
        detectAndLogClipping(buffer);
        captureRecentPeak(buffer, fxOutRecent);
        limiter.process(buffer);
        captureRecentPeak(buffer, finalRecent);
    }

    // ---- Debug/reporting peak accessors (dBFS; -120 if silent) ----
    float getFxInPeakDb()    const noexcept { return toDb(fxInRecent.load(std::memory_order_relaxed)); }
    float getFxOutPeakDb()   const noexcept { return toDb(fxOutRecent.load(std::memory_order_relaxed)); }
    float getFinalPeakDb()   const noexcept { return toDb(finalRecent.load(std::memory_order_relaxed)); }

    // ---- Setters used by PluginProcessor ----
    void setSaturationDrive(float d) { sat.setDrive(d); saturationActive = d > 0.001f; }
    void setSaturationMix  (float m) { sat.setMix(m);   if (m <= 0.001f) saturationActive = false; }

    void setChorusMix(float m) { chorus.setMix(m); }
    void setChorusRate(float r) { chorus.setRate(r); }
    void setChorusDepth(float d) { chorus.setDepth(d); }
    void setChorusMode(int m) { chorus.setMode(m); }


    void setDelayMix(float m) { const float v = choirDensityMode ? juce::jmin(m, 0.03f) : m; delay.setMix(v); delayActive = v > 0.001f; }
    void setDelayTime(float s) { delay.setTimeSeconds(s); }
    void setDelayFeedback(float f) { delay.setFeedback(choirDensityMode ? juce::jmin(f, 0.08f) : f); }

    void setReverbMix(float m) { const float v = choirDensityMode ? juce::jmin(m, 0.22f) : m; reverb.setMix(v); reverbActive = v > 0.001f; }
    void setReverbSize(float s) { reverb.setSize(choirDensityMode ? juce::jmin(s, 0.62f) : s); }
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
    void notifyTransportStopped(int numSamples)
    {
        delay.reset();
        reverb.notifyTransportStopped(numSamples);
    }
    void notifyTransportPlaying()
    {
        reverb.notifyTransportPlaying();
    }

    // ---- Delay scale-safety ----
    void setDelayDucking(float amount, float attackMs = 5.0f, float releaseMs = 140.0f)
    {
        delay.setDucking(amount, attackMs, releaseMs);
    }

    // ---- Note-density-aware send reduction ----
    void setNoteDensityFxReductionEnabled(bool enabled) noexcept { noteDensityFxReductionEnabled = enabled; }
    void setNoteDensityMaxReduction(float amount) noexcept
    {
        maxDensityReduction = juce::jlimit(0.0f, 0.6f, amount);
    }
    void setDelayDensityWeight(float weight) noexcept
    {
        delayDensityWeight = juce::jlimit(0.0f, 1.0f, weight);
    }
    void setReverbDensityWeight(float weight) noexcept
    {
        reverbDensityWeight = juce::jlimit(0.0f, 1.0f, weight);
    }
    void setChoirDensityMode(bool enabled) noexcept
    {
        choirDensityMode = enabled;
        if (choirDensityMode)
            applyChoirModeCaps();
    }
    void setActiveVoiceCountForDensity(int count) noexcept { activeVoiceCountForDensity = juce::jmax(0, count); }

    // ---- Global "scale-safe" preset toggle ----
    // When true, FX writes from the preset applier are gently tamed:
    //  - reverb/delay mix scaled by 0.75
    //  - delay feedback clamp tightened by -0.05
    //  - reverb + delay ducking forced on
    //  - density reduction always enabled
    void setScaleSafeFxMode(bool on) noexcept { scaleSafeFxMode = on; }
    bool getScaleSafeFxMode() const noexcept { return scaleSafeFxMode; }

    void setClearFxTailOnPresetChange(bool on) noexcept { clearTailOnPresetChange = on; }
    bool getClearFxTailOnPresetChange() const noexcept { return clearTailOnPresetChange; }

    // Drains delay + reverb tank. Called by the preset applier when a new
    // preset is loaded so old reverb tails don't bleed into new instruments.
    void clearTimeFxTails()
    {
        delay.reset();
        reverb.reset();
    }

    // Accessors used by the preset-quality reporter.
    DelayBlock&  getDelay()  noexcept { return delay; }
    ReverbBlock& getReverb() noexcept { return reverb; }
    bool getNoteDensityFxReductionEnabled() const noexcept { return noteDensityFxReductionEnabled; }
    float getNoteDensityMaxReduction() const noexcept { return maxDensityReduction; }
    float getDelayDensityWeight() const noexcept { return delayDensityWeight; }
    float getReverbDensityWeight() const noexcept { return reverbDensityWeight; }
    bool getChoirDensityMode() const noexcept { return choirDensityMode; }
    int getActiveVoiceCountForDensity() const noexcept { return activeVoiceCountForDensity; }

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
    void applyChoirModeCaps() noexcept
    {
        delay.setMix(juce::jmin(delay.getMix(), 0.03f));
        delay.setFeedback(juce::jmin(delay.getFeedback(), 0.08f));
        delay.setDucking(0.50f, 5.0f, 140.0f);
        delayActive = delay.getMix() > 0.001f;

        reverb.setMix(juce::jmin(reverb.getMix(), 0.22f));
        reverb.setSize(juce::jmin(reverb.getSize(), 0.62f));
        reverb.setInputHighPassHz(300.0f);
        reverb.setInputLowPassHz(5500.0f);
        reverb.setDucking(juce::jmax(reverb.getDuckAmount(), 0.28f), 6.0f, 220.0f);
        reverbActive = reverb.getMix() > 0.001f;
    }

    // Scan the post-master buffer for samples exceeding ~ -1 dB (0.891).
    // Logs at most ~twice per second per chain instance.
    void detectAndLogClipping(juce::AudioBuffer<float>& buffer) noexcept
    {
        constexpr float kClipThresh = 0.891251f; // -1 dBFS
        float peak = 0.0f;
        const int n = buffer.getNumSamples();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* d = buffer.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
            {
                const float a = std::abs(d[i]);
                if (a > peak) peak = a;
            }
        }
        clipFramesSinceLog += n;
        if (peak > kClipThresh && clipFramesSinceLog > 22050) // ~0.5s @ 44.1k
        {
            juce::Logger::writeToLog("[DIDITAGAIN clip] master peak="
                + juce::String(juce::Decibels::gainToDecibels(peak), 2)
                + " dBFS — limiter engaging");
            clipFramesSinceLog = 0;
        }
    }

    // Generic stage peak meter — logs at most ~1 Hz when stage exceeds -1 dBFS.
    void meterStage(const juce::AudioBuffer<float>& buf, const char* stageName,
                    float& peakAccum, int& framesAccum) noexcept
    {
        float p = 0.0f;
        const int n = buf.getNumSamples();
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const auto* d = buf.getReadPointer(ch);
            for (int i = 0; i < n; ++i) { const float a = std::fabs(d[i]); if (a > p) p = a; }
        }
        if (p > peakAccum) peakAccum = p;
        framesAccum += n;
        if (framesAccum >= 44100)
        {
            if (peakAccum > 0.891251f)
                juce::Logger::writeToLog(juce::String("[DIDITAGAIN fx-peak] WARNING stage=")
                    + stageName + " peak="
                    + juce::String(juce::Decibels::gainToDecibels(peakAccum), 2) + " dBFS");
            peakAccum = 0.0f; framesAccum = 0;
        }
    }

    // Per-block peak capture for the debug quality reporter.
    static void captureRecentPeak(const juce::AudioBuffer<float>& buf,
                                  std::atomic<float>& slot) noexcept
    {
        float p = 0.0f;
        const int n = buf.getNumSamples();
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const auto* d = buf.getReadPointer(ch);
            for (int i = 0; i < n; ++i) { const float a = std::fabs(d[i]); if (a > p) p = a; }
        }
        slot.store(p, std::memory_order_relaxed);
    }

    static float toDb(float lin) noexcept
    {
        return lin > 1.0e-6f ? juce::Decibels::gainToDecibels(lin) : -120.0f;
    }

    // Detect note onsets in the dry buffer (rising-edge envelope crossings)
    // and decay them across ~500ms. The resulting `densityEnv` (0..~1) scales
    // the reverb/delay sends so fast scales/chords don't pile up wet tails.
    void updateNoteDensity(const juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();
        if (n <= 0) return;
        const int nc = buffer.getNumChannels();
        const auto* L = buffer.getReadPointer(0);
        const auto* R = nc > 1 ? buffer.getReadPointer(1) : L;

        // Lazy init time constants for current sample rate from buffer length;
        // exact SR is captured by prepare on each block via reverb.prepare,
        // but we don't have it directly here. Approximate with 44.1k — the
        // density curve is intentionally forgiving.
        constexpr float fastCoef    = 0.002f;   // ~10ms attack
        constexpr float slowCoef    = 0.00003f; // ~500ms decay
        constexpr float onsetThresh = 0.06f;

        for (int i = 0; i < n; ++i)
        {
            const float x = 0.5f * (std::fabs(L[i]) + std::fabs(R[i]));
            // Fast envelope
            densityFast += (x - densityFast) * fastCoef;
            // Detect rising edge above slow envelope by `onsetThresh`.
            if (densityFast > densitySlow + onsetThresh
                && (densityFast - densityLastFast) > 0.0f)
            {
                // Accumulate onset energy; saturate at 1.0.
                densityEnv = juce::jmin(1.0f, densityEnv + 0.35f);
            }
            densityLastFast = densityFast;
            densitySlow += (densityFast - densitySlow) * slowCoef;
            // Decay density env back to 0 over ~500ms.
            densityEnv *= (1.0f - slowCoef * 6.0f);
            if (densityEnv < 0.0f) densityEnv = 0.0f;
        }
    }

    int clipFramesSinceLog = 100000;
    float fxInPeak = 0.0f;  int fxInFrames = 0;
    std::atomic<float> fxInRecent  { 0.0f };
    std::atomic<float> fxOutRecent { 0.0f };
    std::atomic<float> finalRecent { 0.0f };

    // Density-aware FX send reduction state.
    float densityFast = 0.0f;
    float densitySlow = 0.0f;
    float densityLastFast = 0.0f;
    float densityEnv  = 0.0f;
    float maxDensityReduction = 0.32f;
    float delayDensityWeight = 1.0f;
    float reverbDensityWeight = 1.0f;
    bool  noteDensityFxReductionEnabled = true;
    bool  scaleSafeFxMode = true;
    bool  clearTailOnPresetChange = true;
    bool  choirDensityMode = false;
    int   activeVoiceCountForDensity = 0;

    Saturation       sat;

    ChorusBlock      chorus;
    DelayBlock       delay;
    ReverbBlock      reverb;
    EQBlock          eq;
    CompressorBlock  comp;
    LimiterBlock     limiter;
    GainStage        masterGain;

    juce::AudioBuffer<float> dryFxScratch;
    juce::AudioBuffer<float> reverbWetScratch;

    juce::dsp::FirstOrderTPTFilter<float> wetHpL, wetHpR;
    float wetHpHz = 80.0f;

    bool saturationActive = false;
    bool delayActive      = false;
    bool reverbActive     = false;
};
