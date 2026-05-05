#pragma once
//==============================================================================
//  PresetCycleTester — repeatable debug/test mode that cycles presets
//  (including mono <-> poly changes) and reports any mismatch between the
//  queued and applied voice state.
//
//  Each "step":
//   1. Records the requested preset index + the parameter target (mono / poly).
//   2. Triggers loadPreset(index) on the PresetManager.
//   3. Waits up to maxWaitMs for the deferred queue to drain
//      (presetLoadSerial caught up AND no queued change AND
//       appliedMonoMode/appliedPolyphony match the APVTS targets).
//   4. Logs PASS / MISMATCH with full state.
//
//  Designed to be safe to run live — it never touches voices directly,
//  only emulates a user clicking "next preset" repeatedly.
//==============================================================================
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class PresetCycleTester : private juce::Timer
{
public:
    explicit PresetCycleTester (DiditagainProcessor& p) : processor (p) {}
    ~PresetCycleTester() override { stop(); }

    std::function<void (const juce::String&)> onLog;
    std::function<void (int passed, int failed, int total)> onFinished;

    void start (int passes = 1, int intervalMs = 350, int maxWaitMs = 1500)
    {
        stop();
        totalPasses     = juce::jmax (1, passes);
        currentPass     = 0;
        currentIndex    = -1;
        passed = failed = 0;
        waitElapsedMs   = 0;
        intervalMsConfig = intervalMs;
        maxWaitMsConfig  = maxWaitMs;
        phase           = Phase::PickNext;
        running         = true;

        log ("=== PresetCycleTester START — passes=" + juce::String (totalPasses)
             + " presets=" + juce::String (processor.getPresetManager().getNumPresets())
             + " interval=" + juce::String (intervalMs) + "ms"
             + " maxWait=" + juce::String (maxWaitMs) + "ms ===");

        startTimer (50);
    }

    void stop()
    {
        if (! running) return;
        running = false;
        stopTimer();
        log ("=== PresetCycleTester STOP — passed=" + juce::String (passed)
             + " failed=" + juce::String (failed) + " ===");
        if (onFinished) onFinished (passed, failed, passed + failed);
    }

    bool isRunning() const noexcept { return running; }

private:
    enum class Phase { PickNext, WaitForApply };

    void timerCallback() override
    {
        if (! running) return;

        auto& pm = processor.getPresetManager();
        const int n = pm.getNumPresets();
        if (n <= 0) { stop(); return; }

        if (phase == Phase::PickNext)
        {
            ++currentIndex;
            if (currentIndex >= n)
            {
                ++currentPass;
                if (currentPass >= totalPasses) { stop(); return; }
                currentIndex = 0;
            }

            requestedSerialBefore = processor.getRequestedPresetSerial();
            pm.loadPreset (currentIndex);

            // Snapshot the desired target so we know what we're waiting for.
            expectedMono = paramBool ("monoMode");
            expectedPoly = juce::jlimit (1, 16, paramInt ("polyphony"));

            log ("[STEP] pass=" + juce::String (currentPass + 1) + "/" + juce::String (totalPasses)
                 + " idx=" + juce::String (currentIndex)
                 + " name=\"" + pm.getPresetName (currentIndex) + "\""
                 + " expectMono=" + boolStr (expectedMono)
                 + " expectPoly=" + juce::String (expectedPoly));

            waitElapsedMs = 0;
            phase = Phase::WaitForApply;
            startTimer (25); // tighter polling while we wait
            return;
        }

        // Phase::WaitForApply
        waitElapsedMs += getTimerInterval();

        const int  reqSerial   = processor.getRequestedPresetSerial();
        const int  obsSerial   = processor.getObservedPresetSerial();
        const bool queued      = processor.isPresetChangeQueued();
        const bool appliedMono = processor.getAppliedMonoMode();
        const int  appliedPoly = processor.getAppliedPolyphony();

        // Re-read targets each tick — APVTS may settle a tick after preset load.
        expectedMono = paramBool ("monoMode");
        expectedPoly = juce::jlimit (1, 16, paramInt ("polyphony"));

        const bool serialCaughtUp = (obsSerial >= reqSerial);
        const bool monoMatches    = (appliedMono == expectedMono);
        const bool polyMatches    = expectedMono ? true : (appliedPoly == expectedPoly);
        const bool fullyApplied   = serialCaughtUp && ! queued && monoMatches && polyMatches;

        if (fullyApplied)
        {
            ++passed;
            log ("  PASS  serial=" + juce::String (obsSerial)
                 + " mono=" + boolStr (appliedMono)
                 + " poly=" + juce::String (appliedPoly)
                 + " waited=" + juce::String (waitElapsedMs) + "ms");
            phase = Phase::PickNext;
            startTimer (intervalMsConfig);
            return;
        }

        if (waitElapsedMs >= maxWaitMsConfig)
        {
            ++failed;
            juce::StringArray reasons;
            if (! serialCaughtUp) reasons.add ("serial(" + juce::String (obsSerial)
                                                + "<" + juce::String (reqSerial) + ")");
            if (queued)            reasons.add ("queued");
            if (! monoMatches)     reasons.add ("mono(applied=" + boolStr (appliedMono)
                                                + " expected=" + boolStr (expectedMono) + ")");
            if (! polyMatches)     reasons.add ("poly(applied=" + juce::String (appliedPoly)
                                                + " expected=" + juce::String (expectedPoly) + ")");

            log ("  MISMATCH idx=" + juce::String (currentIndex)
                 + " after " + juce::String (waitElapsedMs) + "ms — "
                 + reasons.joinIntoString (", "));

            phase = Phase::PickNext;
            startTimer (intervalMsConfig);
        }
    }

    bool paramBool (const char* id) const
    {
        if (auto* p = processor.getAPVTS().getRawParameterValue (id))
            return p->load() > 0.5f;
        return false;
    }
    int paramInt (const char* id) const
    {
        if (auto* p = processor.getAPVTS().getRawParameterValue (id))
            return static_cast<int> (p->load());
        return 0;
    }

    static juce::String boolStr (bool b) { return b ? "true" : "false"; }

    void log (const juce::String& s)
    {
        const auto line = "[DIDITAGAIN cycle-test] " + s;
        juce::Logger::writeToLog (line);
        if (onLog) onLog (line);
    }

    DiditagainProcessor& processor;
    bool  running = false;
    Phase phase = Phase::PickNext;
    int   currentPass = 0;
    int   totalPasses = 1;
    int   currentIndex = -1;
    int   requestedSerialBefore = 0;
    bool  expectedMono = false;
    int   expectedPoly = 8;
    int   waitElapsedMs = 0;
    int   intervalMsConfig = 350;
    int   maxWaitMsConfig  = 1500;
    int   passed = 0;
    int   failed = 0;
};
