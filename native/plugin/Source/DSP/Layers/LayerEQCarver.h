#pragma once
//==============================================================================
//  LayerEQCarver.h — Tiny one-pole high/low-shelf carvers used per-layer
//  inside SynthVoice to keep stacked layers in their own spectral lane.
//
//  Each instance is one biquad-free TPT one-pole; cheap enough to run per
//  voice per layer. Used to:
//    - high-pass noise/air layers around 2 kHz
//    - low-pass sub/body around 250-400 Hz
//    - high-shelf for "shimmer" presence
//==============================================================================
#include <JuceHeader.h>
#include <cmath>
#include <algorithm>


class OnePoleCarver
{
public:
    enum class Mode { LowPass, HighPass };

    void prepare(double sr) noexcept { sampleRate = sr; updateCoef(); }
    void setMode(Mode m)    noexcept { mode = m; }
    void setCutoff(float hz) noexcept
    {
        cutoff = std::clamp(hz, 20.0f, 20000.0f);
        updateCoef();
    }

    inline float process(float x) noexcept
    {
        // TPT one-pole low-pass
        const float v = (x - z) * g;
        const float lp = v + z;
        z = lp + v;
        return mode == Mode::LowPass ? lp : (x - lp);
    }

    void reset() noexcept { z = 0.0f; }

private:
    void updateCoef() noexcept
    {
        const float wd = 2.0f * 3.14159265358979323846f * cutoff;
        const float T  = 1.0f / (float) sampleRate;
        const float wa = (2.0f / T) * std::tan(wd * T * 0.5f);
        g = wa * T * 0.5f / (1.0f + wa * T * 0.5f);
    }

    double sampleRate = 44100.0;
    float  cutoff = 8000.0f;
    float  g = 0.0f, z = 0.0f;
    Mode   mode = Mode::LowPass;
};

//==============================================================================
//  LayerRoleCarver — role-aware EQ shaping (HP+LP+trim) for a single layer.
//
//  Implements the v2.1 eqRole contract. Roles:
//    body    — full-range main layer, gentle LP shelf only if needed
//    warmth  — HP ~150 Hz, LP ~2 kHz  (tame harsh upper-mids)
//    air     — HP ~2.5 kHz, low gain  (shimmer only)
//    texture — HP ~900 Hz, LP ~5.5 kHz, quiet colored layer
//    sub     — LP ~150 Hz, narrow (only musical for 808/bass)
//    lead    — minimal carving, allowed presence
//    full    — no automatic carving (experimental opt-in)
//
//  Always-on: pass setRole("") or "auto" to apply a safe default (warmth).
//==============================================================================
class LayerRoleCarver
{
public:
    void prepare(double sr) noexcept
    {
        hp.prepare(sr); hp.setMode(OnePoleCarver::Mode::HighPass);
        lp.prepare(sr); lp.setMode(OnePoleCarver::Mode::LowPass);
        applyRoleSettings();
    }

    void reset() noexcept { hp.reset(); lp.reset(); }

    // Set the active role string. Unknown -> "warmth" fallback.
    void setRole(const juce::String& roleIn) noexcept
    {
        const auto r = roleIn.trim().toLowerCase();
        if (r == role) return;
        role = r;
        applyRoleSettings();
    }

    float getTrimLinear() const noexcept { return trimLin; }
    bool  isFullPass()    const noexcept { return role == "full" || role == "lead"; }

    inline float process(float x) noexcept
    {
        if (role == "full") return x;          // no carving
        if (role == "lead")
        {
            // Lead: only gentle HP (rumble) + trim, no LP carve.
            return hp.process(x) * trimLin;
        }
        float y = hp.process(x);
        y = lp.process(y);
        return y * trimLin;
    }

private:
    void applyRoleSettings() noexcept
    {
        // Defaults
        float hpHz = 20.0f, lpHz = 20000.0f, trimDb = 0.0f;

        if      (role == "body")    { hpHz =   30.0f; lpHz = 16000.0f; trimDb =  0.0f; }
        else if (role == "warmth")  { hpHz =  160.0f; lpHz =  2400.0f; trimDb = -2.0f; }
        else if (role == "air")     { hpHz = 2600.0f; lpHz = 18000.0f; trimDb = -6.0f; }
        else if (role == "texture") { hpHz =  900.0f; lpHz =  5500.0f; trimDb = -4.0f; }
        // AI Texture v0.1: keep neural layers out of the main sample's body so
        // cached textures add air/colour without muddying the multisample.
        else if (role == "neuraltexture") { hpHz = 500.0f; lpHz = 12000.0f; trimDb = -8.0f; }
        else if (role == "sub")     { hpHz =   20.0f; lpHz =   150.0f; trimDb = -2.0f; }
        else if (role == "lead")    { hpHz =   80.0f; lpHz = 19000.0f; trimDb =  0.0f; }
        else if (role == "full")    { hpHz =   20.0f; lpHz = 20000.0f; trimDb =  0.0f; }
        else /* auto / unknown */   { hpHz =  150.0f; lpHz =  3000.0f; trimDb = -3.0f; }

        hp.setCutoff(hpHz);
        lp.setCutoff(lpHz);
        trimLin = std::pow(10.0f, trimDb / 20.0f);
    }

    OnePoleCarver hp, lp;
    juce::String  role;
    float         trimLin = 1.0f;
};


