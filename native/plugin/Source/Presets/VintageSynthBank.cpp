#include "VintageSynthBank.h"

// ============================================================================
// VintageSynthBank.cpp
//
// 20 vintage-inspired synth presets, retuned for SAFE gain staging.
// Hard rules (matches the Family::Synth clamps in UserPresetLoader.cpp):
//   gainDb       : -6 .. -3 dB
//   attack       :  8 .. 40 ms (slow pads use macro.attack instead)
//   release      : 400 .. 1400 ms (longer pads still allowed, clamped upstream)
//   filter cutoff: 3000 .. 8000 Hz
//   resonance    : 0.08 .. 0.18
//   saturation   : drive 0.08-0.16, mix 0.06-0.14 (clean vintage)
//   layer2 gain  : -24 .. -18 dB (just a whisper of detune, no octave stacking)
//   chorus mix   : 0.08 .. 0.18
//   delay mix    : 0.04 .. 0.10
//   reverb mix   : 0.08 .. 0.18
//
// One "intentionally dirty" preset (Horror Lead) opts into experimental=true,
// which lets it bend the FX-cap rules a bit (still bounded by Family::Synth's
// hard caps in the loader). The output is also protected by a brickwall
// limiter at -0.3 dB plus a clip-warning log in FxChain.
// ============================================================================

namespace dida { namespace userpreset {

// Sensible neutral starting point — every preset overrides what it needs.
static UserPreset makeBase (const juce::String& name, const juce::String& src)
{
    UserPreset p;
    p.presetName = name;
    p.category   = "Synth";

    p.source.type            = "multisampleFolder";
    p.source.path            = src;
    p.source.mappingMode     = "nearest";
    p.source.rootNotePattern = { "C", "D#", "F#", "A" };

    // amp: master at -4 dB headroom, modest envelope.
    p.amp    = { -4.0f, 0.0f, 12.0f, 320.0f, 0.80f, 700.0f };
    p.filter = { true, "lowpass", 5500.0f, 0.12f, 0.05f, 0.18f };

    p.main   = { true,   0.0f, 0.0f, 0, 0,  0.0f };
    // Layer 2 = quiet detuned twin, never octave-stacked by default.
    p.layer2 = { true,  -20.0f, 0.0f, 0, 0, +4.0f };

    p.chorus     = { true,  0.30f, 0.25f, 0.14f };   // gentle BBD-style
    p.delay      = { false, 320.0f, 0.25f, 0.06f };  // disabled by default
    p.reverb     = { true,  0.45f, 0.45f, 0.14f };   // light room
    p.saturation = { true,  0.10f, 0.10f };          // subtle tape glue only

    p.lfo1 = { true,  "filter.cutoffHz", "sine",     0.18f, 0.08f };
    p.lfo2 = { true,  "amp.pan",         "triangle", 0.09f, 0.05f };

    p.advanced = { 0.0f, 0.0f, 0.30f, 0.20f, 3.0f, 1.2f, 8 };

    // v2 — warm, conservative defaults.
    p.macros   = { /*tone*/0.50f, /*move*/0.25f, /*width*/0.60f, /*warmth*/0.45f,
                   /*atk*/0.30f,  /*rel*/0.50f,  /*space*/0.35f, /*char*/0.45f };
    p.velocity = { 0.40f, 0.25f, 0.15f, 0.30f };
    p.layerEq  = { 0.0f, 0.0f, 260.0f, 0.0f };
    p.filterMovement = { true, 0.14f, 0.18f };
    p.experimental = false;
    return p;
}

juce::Array<UserPreset> buildVintageSynthBank (const juce::String& src)
{
    juce::Array<UserPreset> out;
    auto add = [&](UserPreset p) { out.add (std::move (p)); };

    // 1. Juno-60 Warm Pad
    { auto p = makeBase ("Juno-60 Warm Pad", src);
      p.amp = { -5.0f, 0.0f, 40.0f, 700.0f, 0.85f, 1400.0f };
      p.filter.cutoffHz = 3400.0f; p.filter.resonance = 0.14f;
      p.chorus = { true, 0.45f, 0.35f, 0.18f };
      p.reverb = { true, 0.55f, 0.45f, 0.18f };
      p.macros.width = 0.80f; p.macros.warmth = 0.55f; p.macros.space = 0.55f;
      add (p); }

    // 2. Jupiter-8 Brass
    { auto p = makeBase ("Jupiter-8 Brass", src);
      p.amp = { -4.0f, 0.0f, 18.0f, 380.0f, 0.75f, 480.0f };
      p.filter.cutoffHz = 5200.0f; p.filter.resonance = 0.18f; p.filter.drive = 0.08f;
      p.layer2.detuneCents = +5.0f; p.layer2.gainDb = -20.0f;
      p.macros.tone = 0.60f; p.macros.warmth = 0.55f;
      p.velocity.toCutoff = 0.40f; p.velocity.toGain = 0.45f;
      add (p); }

    // 3. Prophet-5 Soft Keys
    { auto p = makeBase ("Prophet-5 Soft Keys", src);
      p.amp = { -4.0f, 0.0f, 10.0f, 260.0f, 0.55f, 420.0f };
      p.filter.cutoffHz = 4600.0f; p.filter.resonance = 0.10f;
      p.chorus = { true, 0.35f, 0.22f, 0.12f };
      p.macros.tone = 0.55f; p.macros.movement = 0.18f; p.macros.space = 0.30f;
      add (p); }

    // 4. CS-80 Cinematic Pad
    { auto p = makeBase ("CS-80 Cinematic Pad", src);
      p.amp = { -5.0f, 0.0f, 40.0f, 1100.0f, 0.88f, 1400.0f };
      p.filter.cutoffHz = 3200.0f; p.filter.resonance = 0.16f;
      p.reverb = { true, 0.65f, 0.45f, 0.18f };
      p.macros.movement = 0.45f; p.macros.width = 0.85f; p.macros.space = 0.60f;
      p.filterMovement = { true, 0.22f, 0.12f };
      add (p); }

    // 5. Analog Bass Mono
    { auto p = makeBase ("Analog Bass Mono", src);
      p.amp = { -3.0f, 0.0f, 8.0f, 220.0f, 0.65f, 400.0f };
      p.filter.cutoffHz = 3000.0f; p.filter.resonance = 0.18f; p.filter.drive = 0.12f;
      p.layer2.enabled = false;
      p.chorus.enabled = false;
      p.reverb.mix = 0.08f;
      p.saturation = { true, 0.14f, 0.12f };
      p.macros.width = 0.15f; p.macros.warmth = 0.65f; p.macros.space = 0.10f;
      p.advanced.polyphony = 1;
      add (p); }

    // 6. Mono Lead Classic
    { auto p = makeBase ("Mono Lead Classic", src);
      p.amp = { -4.0f, 0.0f, 10.0f, 240.0f, 0.80f, 420.0f };
      p.filter.cutoffHz = 5800.0f; p.filter.resonance = 0.18f;
      p.chorus = { true, 0.35f, 0.30f, 0.16f };
      p.macros.tone = 0.60f; p.macros.movement = 0.22f;
      p.advanced.polyphony = 1;
      add (p); }

    // 7. PWM Pad
    { auto p = makeBase ("PWM Pad", src);
      p.amp = { -5.0f, 0.0f, 35.0f, 900.0f, 0.85f, 1400.0f };
      p.filter.cutoffHz = 3200.0f;
      p.lfo1 = { true, "osc.pulseWidth", "sine", 0.22f, 0.30f };
      p.macros.movement = 0.55f; p.macros.width = 0.75f;
      add (p); }

    // 8. String Machine
    { auto p = makeBase ("String Machine", src);
      p.amp = { -5.0f, 0.0f, 35.0f, 700.0f, 0.85f, 1400.0f };
      p.filter.cutoffHz = 5200.0f; p.filter.resonance = 0.10f;
      p.chorus = { true, 0.55f, 0.45f, 0.18f };
      p.reverb = { true, 0.55f, 0.45f, 0.18f };
      p.macros.width = 0.85f; p.macros.warmth = 0.45f;
      add (p); }

    // 9. Vintage Pluck
    { auto p = makeBase ("Vintage Pluck", src);
      p.amp = { -4.0f, 0.0f, 8.0f, 180.0f, 0.00f, 400.0f };
      p.filter.cutoffHz = 4200.0f; p.filter.resonance = 0.14f;
      p.delay = { true, 280.0f, 0.25f, 0.08f };
      p.macros.tone = 0.55f; p.macros.movement = 0.10f;
      add (p); }

    // 10. Analog Bell
    { auto p = makeBase ("Analog Bell", src);
      p.amp = { -4.0f, 0.0f, 8.0f, 800.0f, 0.20f, 1400.0f };
      p.filter.cutoffHz = 7800.0f; p.filter.resonance = 0.12f;
      p.layerEq.layer2BodyHz = 480.0f;
      p.macros.tone = 0.70f; p.macros.space = 0.55f; p.macros.character = 0.65f;
      add (p); }

    // 11. Dark Choir
    { auto p = makeBase ("Dark Choir", src);
      p.amp = { -5.0f, 0.0f, 40.0f, 1200.0f, 0.85f, 1400.0f };
      p.filter.cutoffHz = 3000.0f; p.filter.resonance = 0.10f;
      p.reverb = { true, 0.70f, 0.55f, 0.18f };
      p.macros.tone = 0.35f; p.macros.space = 0.65f;
      add (p); }

    // 12. Reese Bass
    { auto p = makeBase ("Reese Bass", src);
      p.amp = { -3.0f, 0.0f, 12.0f, 260.0f, 0.80f, 420.0f };
      p.filter.cutoffHz = 3000.0f; p.filter.resonance = 0.18f; p.filter.drive = 0.14f;
      p.layer2.detuneCents = +7.0f; p.layer2.gainDb = -18.0f;
      p.saturation = { true, 0.14f, 0.12f };
      p.macros.width = 0.30f; p.macros.warmth = 0.65f;
      p.advanced.polyphony = 2;
      add (p); }

    // 13. Soft Poly Keys
    { auto p = makeBase ("Soft Poly Keys", src);
      p.amp = { -4.0f, 0.0f, 10.0f, 320.0f, 0.55f, 480.0f };
      p.filter.cutoffHz = 5400.0f; p.filter.resonance = 0.10f;
      p.chorus = { true, 0.30f, 0.20f, 0.12f };
      p.macros.tone = 0.55f; p.macros.warmth = 0.45f;
      add (p); }

    // 14. Horror Lead — intentionally dirty (experimental).
    { auto p = makeBase ("Horror Lead", src);
      p.amp = { -4.0f, 0.0f, 30.0f, 600.0f, 0.85f, 900.0f };
      p.filter.cutoffHz = 3000.0f; p.filter.resonance = 0.18f; p.filter.drive = 0.16f;
      p.layer2.detuneCents = +10.0f; p.layer2.gainDb = -18.0f;
      p.saturation = { true, 0.16f, 0.14f };
      p.filterMovement = { true, 0.30f, 0.10f };
      p.macros.tone = 0.40f; p.macros.movement = 0.55f;
      p.experimental = true;
      add (p); }

    // 15. Tape Pad
    { auto p = makeBase ("Tape Pad", src);
      p.amp = { -5.0f, 0.0f, 40.0f, 1100.0f, 0.85f, 1400.0f };
      p.filter.cutoffHz = 3000.0f;
      p.saturation = { true, 0.14f, 0.12f };
      p.advanced.humanizePitchCents = 5.0f; p.advanced.humanizeTimingMs = 4.0f;
      p.macros.warmth = 0.70f; p.macros.character = 0.35f;
      add (p); }

    // 16. Analog Init
    { auto p = makeBase ("Analog Init", src);
      p.layer2.enabled = false;
      p.chorus.mix = 0.12f; p.reverb.mix = 0.10f;
      p.macros.movement = 0.18f; p.macros.width = 0.50f;
      add (p); }

    // 17. Brass Stack
    { auto p = makeBase ("Brass Stack", src);
      p.amp = { -4.0f, 0.0f, 20.0f, 380.0f, 0.80f, 520.0f };
      p.filter.cutoffHz = 4800.0f; p.filter.resonance = 0.16f; p.filter.drive = 0.10f;
      p.chorus = { true, 0.45f, 0.40f, 0.18f };
      p.macros.width = 0.80f; p.macros.warmth = 0.55f;
      p.velocity.toCutoff = 0.35f;
      add (p); }

    // 18. Glass Pad
    { auto p = makeBase ("Glass Pad", src);
      p.amp = { -5.0f, 0.0f, 35.0f, 900.0f, 0.80f, 1400.0f };
      p.filter.cutoffHz = 6500.0f;
      p.reverb = { true, 0.60f, 0.30f, 0.18f };
      p.macros.tone = 0.70f; p.macros.character = 0.70f; p.macros.space = 0.65f;
      add (p); }

    // 19. Sequencer Pulse
    { auto p = makeBase ("Sequencer Pulse", src);
      p.amp = { -4.0f, 0.0f, 8.0f, 140.0f, 0.10f, 400.0f };
      p.filter.cutoffHz = 3200.0f; p.filter.resonance = 0.18f;
      p.lfo1 = { true, "filter.cutoffHz", "sine", 4.5f, 0.30f };
      p.delay = { true, 250.0f, 0.30f, 0.10f };
      p.macros.movement = 0.50f; p.macros.width = 0.55f;
      add (p); }

    // 20. Ambient Drone
    { auto p = makeBase ("Ambient Drone", src);
      p.amp = { -5.0f, 0.0f, 40.0f, 1400.0f, 0.92f, 1400.0f };
      p.filter.cutoffHz = 3000.0f; p.filter.resonance = 0.16f;
      p.reverb = { true, 0.75f, 0.55f, 0.18f };
      p.filterMovement = { true, 0.30f, 0.06f };
      p.macros.movement = 0.65f; p.macros.width = 0.90f; p.macros.space = 0.75f;
      add (p); }

    return out;
}

}} // namespace dida::userpreset
