#include "VintageSynthBank.h"

namespace dida { namespace userpreset {

// Sensible neutral starting point — every preset overrides what it needs.
// The category "Synth" routes through HybridPresetApplier's synth voicing,
// and the macro/velocity/filterMovement blocks (v2 schema) drive the
// vintage-modeling layer-bus and per-voice analog drift.
static UserPreset makeBase (const juce::String& name, const juce::String& src)
{
    UserPreset p;
    p.presetName = name;
    p.category   = "Synth";

    p.source.type            = "multisampleFolder";
    p.source.path            = src;
    p.source.mappingMode     = "nearest";
    p.source.rootNotePattern = { "C", "D#", "F#", "A" };

    p.amp    = { 0.0f, 0.0f, 8.0f, 350.0f, 0.85f, 500.0f };
    p.filter = { true, "lowpass", 8000.0f, 0.15f, 0.10f, 0.20f };
    p.main   = { true,   0.0f, 0.0f, 0, 0,  0.0f };
    p.layer2 = { true,  -6.0f, 0.0f, 0, 0, +7.0f };   // detuned twin osc

    p.chorus     = { true,  0.40f, 0.30f, 0.35f };    // BBD-style by default
    p.delay      = { false, 320.0f, 0.30f, 0.0f };
    p.reverb     = { true,  0.55f, 0.45f, 0.22f };
    p.saturation = { true,  0.12f, 0.20f };

    p.lfo1 = { true,  "filter.cutoffHz", "sine",     0.18f, 0.10f };
    p.lfo2 = { true,  "amp.pan",         "triangle", 0.09f, 0.08f };

    p.advanced = { 0.0f, 0.0f, 0.30f, 0.25f, 4.0f, 1.5f, 8 };

    // v2 — vintage-leaning defaults
    p.macros   = { /*tone*/0.55f, /*move*/0.35f, /*width*/0.65f, /*warmth*/0.50f,
                   /*atk*/0.30f,  /*rel*/0.55f,  /*space*/0.40f, /*char*/0.45f };
    p.velocity = { 0.40f, 0.30f, 0.15f, 0.35f };
    p.layerEq  = { 0.0f, 0.0f, 260.0f, 0.0f };
    p.filterMovement = { true, 0.18f, 0.18f };
    p.experimental = false;
    return p;
}

juce::Array<UserPreset> buildVintageSynthBank (const juce::String& src)
{
    juce::Array<UserPreset> out;
    auto add = [&](UserPreset p) { out.add (std::move (p)); };

    // 1. Juno-60 Warm Pad — wide chorus, gentle filter, slow attack
    { auto p = makeBase ("Juno-60 Warm Pad", src);
      p.amp = { 0.0f, 0.0f, 600.0f, 800.0f, 0.85f, 1800.0f };
      p.filter.cutoffHz = 3400.0f; p.filter.resonance = 0.18f;
      p.chorus = { true, 0.55f, 0.45f, 0.55f };
      p.macros.width = 0.85f; p.macros.warmth = 0.55f; p.macros.space = 0.55f;
      add (p); }

    // 2. Jupiter-8 Brass — punchy attack, env-modulated filter, stereo wash
    { auto p = makeBase ("Jupiter-8 Brass", src);
      p.amp = { 0.0f, 0.0f, 18.0f, 420.0f, 0.75f, 480.0f };
      p.filter.cutoffHz = 5200.0f; p.filter.resonance = 0.25f; p.filter.drive = 0.22f;
      p.layer2.detuneCents = +9.0f;
      p.macros.tone = 0.65f; p.macros.warmth = 0.60f;
      p.velocity.toCutoff = 0.45f; p.velocity.toGain = 0.50f;
      add (p); }

    // 3. Prophet-5 Soft Keys — short release, light chorus, focused mids
    { auto p = makeBase ("Prophet-5 Soft Keys", src);
      p.amp = { 0.0f, 0.0f, 6.0f, 280.0f, 0.55f, 380.0f };
      p.filter.cutoffHz = 4600.0f; p.filter.resonance = 0.12f;
      p.chorus = { true, 0.35f, 0.22f, 0.30f };
      p.macros.tone = 0.55f; p.macros.movement = 0.20f; p.macros.space = 0.30f;
      add (p); }

    // 4. CS-80 Cinematic Pad — dual filter character, expressive aftertouch
    { auto p = makeBase ("CS-80 Cinematic Pad", src);
      p.amp = { 0.0f, 0.0f, 900.0f, 1400.0f, 0.90f, 2600.0f };
      p.filter.cutoffHz = 2800.0f; p.filter.resonance = 0.22f;
      p.reverb = { true, 0.75f, 0.45f, 0.45f };
      p.macros.movement = 0.55f; p.macros.width = 0.90f; p.macros.space = 0.65f;
      p.filterMovement = { true, 0.30f, 0.12f };
      add (p); }

    // 5. Analog Bass Mono — tight, mono, saturated low end
    { auto p = makeBase ("Analog Bass Mono", src);
      p.amp = { 0.0f, 0.0f, 3.0f, 220.0f, 0.65f, 240.0f };
      p.filter.cutoffHz = 900.0f; p.filter.resonance = 0.35f; p.filter.drive = 0.30f;
      p.layer2.enabled = false;
      p.chorus.enabled = false; p.reverb.mix = 0.05f;
      p.macros.width = 0.10f; p.macros.warmth = 0.65f; p.macros.space = 0.10f;
      p.advanced.polyphony = 1;
      add (p); }

    // 6. Mono Lead Classic — singing lead, mild glide, juicy chorus
    { auto p = makeBase ("Mono Lead Classic", src);
      p.amp = { 0.0f, 0.0f, 8.0f, 240.0f, 0.80f, 320.0f };
      p.filter.cutoffHz = 5800.0f; p.filter.resonance = 0.30f;
      p.chorus = { true, 0.45f, 0.40f, 0.40f };
      p.macros.tone = 0.65f; p.macros.movement = 0.25f;
      p.advanced.polyphony = 1;
      add (p); }

    // 7. PWM Pad — pulse-width-modulated breathing pad
    { auto p = makeBase ("PWM Pad", src);
      p.amp = { 0.0f, 0.0f, 700.0f, 1200.0f, 0.85f, 2200.0f };
      p.filter.cutoffHz = 3200.0f;
      p.lfo1 = { true, "osc.pulseWidth", "sine", 0.22f, 0.45f };
      p.macros.movement = 0.65f; p.macros.width = 0.80f;
      add (p); }

    // 8. String Machine — Solina-style ensemble strings
    { auto p = makeBase ("String Machine", src);
      p.amp = { 0.0f, 0.0f, 420.0f, 900.0f, 0.85f, 1400.0f };
      p.filter.cutoffHz = 5200.0f; p.filter.resonance = 0.10f;
      p.chorus = { true, 0.60f, 0.55f, 0.65f };
      p.reverb = { true, 0.65f, 0.45f, 0.30f };
      p.macros.width = 0.90f; p.macros.warmth = 0.45f;
      add (p); }

    // 9. Vintage Pluck — short, percussive, slightly detuned
    { auto p = makeBase ("Vintage Pluck", src);
      p.amp = { 0.0f, 0.0f, 2.0f, 180.0f, 0.00f, 300.0f };
      p.filter.cutoffHz = 4200.0f; p.filter.resonance = 0.18f;
      p.delay = { true, 280.0f, 0.32f, 0.18f };
      p.macros.tone = 0.60f; p.macros.movement = 0.10f;
      add (p); }

    // 10. Analog Bell — clean transient, long decay, sparkly air
    { auto p = makeBase ("Analog Bell", src);
      p.amp = { 0.0f, 0.0f, 1.0f, 900.0f, 0.20f, 1600.0f };
      p.filter.cutoffHz = 7800.0f; p.filter.resonance = 0.15f;
      p.layerEq.layer2BodyHz = 480.0f;
      p.macros.tone = 0.75f; p.macros.space = 0.55f; p.macros.character = 0.70f;
      add (p); }

    // 11. Dark Choir — slow attack, dark voicing, dense reverb
    { auto p = makeBase ("Dark Choir", src);
      p.amp = { 0.0f, 0.0f, 1100.0f, 1800.0f, 0.85f, 3000.0f };
      p.filter.cutoffHz = 2200.0f; p.filter.resonance = 0.10f;
      p.reverb = { true, 0.80f, 0.55f, 0.45f };
      p.macros.tone = 0.30f; p.macros.space = 0.70f;
      add (p); }

    // 12. Reese Bass — thick detuned saws, mono low end
    { auto p = makeBase ("Reese Bass", src);
      p.amp = { 0.0f, 0.0f, 4.0f, 260.0f, 0.80f, 320.0f };
      p.filter.cutoffHz = 1400.0f; p.filter.resonance = 0.32f; p.filter.drive = 0.30f;
      p.layer2.detuneCents = +12.0f; p.layer2.gainDb = -3.0f;
      p.macros.width = 0.30f; p.macros.warmth = 0.70f;
      p.advanced.polyphony = 2;
      add (p); }

    // 13. Soft Poly Keys — gentle bell-like keys, light air
    { auto p = makeBase ("Soft Poly Keys", src);
      p.amp = { 0.0f, 0.0f, 5.0f, 320.0f, 0.55f, 480.0f };
      p.filter.cutoffHz = 5400.0f; p.filter.resonance = 0.12f;
      p.chorus = { true, 0.30f, 0.20f, 0.28f };
      p.macros.tone = 0.55f; p.macros.warmth = 0.45f;
      add (p); }

    // 14. Horror Lead — detuned, slow filter sweep, aggressive saturation
    { auto p = makeBase ("Horror Lead", src);
      p.amp = { 0.0f, 0.0f, 30.0f, 600.0f, 0.85f, 900.0f };
      p.filter.cutoffHz = 1800.0f; p.filter.resonance = 0.55f; p.filter.drive = 0.45f;
      p.layer2.detuneCents = +18.0f;
      p.saturation = { true, 0.30f, 0.35f };
      p.filterMovement = { true, 0.45f, 0.08f };
      p.macros.tone = 0.35f; p.macros.movement = 0.65f;
      p.experimental = true;
      add (p); }

    // 15. Tape Pad — lo-fi wow/flutter pad with darker air
    { auto p = makeBase ("Tape Pad", src);
      p.amp = { 0.0f, 0.0f, 800.0f, 1400.0f, 0.85f, 2400.0f };
      p.filter.cutoffHz = 3000.0f;
      p.saturation = { true, 0.25f, 0.30f };
      p.advanced.humanizePitchCents = 9.0f; p.advanced.humanizeTimingMs = 6.0f;
      p.macros.warmth = 0.75f; p.macros.character = 0.30f;
      add (p); }

    // 16. Analog Init — clean starting point, mild vintage
    { auto p = makeBase ("Analog Init", src);
      // keep defaults except disable layer 2 for a single-osc init
      p.layer2.enabled = false;
      p.chorus.mix = 0.20f; p.reverb.mix = 0.15f;
      p.macros.movement = 0.20f; p.macros.width = 0.50f;
      add (p); }

    // 17. Brass Stack — layered brass, wide stereo, BBD chorus II
    { auto p = makeBase ("Brass Stack", src);
      p.amp = { 0.0f, 0.0f, 20.0f, 380.0f, 0.80f, 520.0f };
      p.filter.cutoffHz = 4800.0f; p.filter.resonance = 0.20f; p.filter.drive = 0.20f;
      p.chorus = { true, 0.55f, 0.50f, 0.50f };
      p.macros.width = 0.85f; p.macros.warmth = 0.55f;
      p.velocity.toCutoff = 0.40f;
      add (p); }

    // 18. Glass Pad — sparkly air, shimmer reverb feel
    { auto p = makeBase ("Glass Pad", src);
      p.amp = { 0.0f, 0.0f, 600.0f, 1100.0f, 0.80f, 2000.0f };
      p.filter.cutoffHz = 6500.0f;
      p.reverb = { true, 0.70f, 0.30f, 0.40f };
      p.macros.tone = 0.75f; p.macros.character = 0.80f; p.macros.space = 0.70f;
      add (p); }

    // 19. Sequencer Pulse — short gate, syncopation-friendly, mono-ish
    { auto p = makeBase ("Sequencer Pulse", src);
      p.amp = { 0.0f, 0.0f, 2.0f, 140.0f, 0.10f, 220.0f };
      p.filter.cutoffHz = 3000.0f; p.filter.resonance = 0.40f;
      p.lfo1 = { true, "filter.cutoffHz", "sine", 4.5f, 0.35f };
      p.delay = { true, 250.0f, 0.40f, 0.20f };
      p.macros.movement = 0.55f; p.macros.width = 0.55f;
      add (p); }

    // 20. Ambient Drone — long evolving drone, deep movement
    { auto p = makeBase ("Ambient Drone", src);
      p.amp = { 0.0f, 0.0f, 1400.0f, 2200.0f, 0.95f, 4500.0f };
      p.filter.cutoffHz = 2400.0f; p.filter.resonance = 0.20f;
      p.reverb = { true, 0.85f, 0.55f, 0.55f };
      p.filterMovement = { true, 0.45f, 0.06f };
      p.macros.movement = 0.75f; p.macros.width = 0.95f; p.macros.space = 0.80f;
      add (p); }

    return out;
}

}} // namespace dida::userpreset
