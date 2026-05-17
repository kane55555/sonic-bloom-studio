#include "GuitarPresetBank.h"

namespace dida { namespace userpreset {

static UserPreset makeBase(const juce::String& name, const juce::String& src)
{
    UserPreset p;
    p.presetName = name;
    p.category   = "Guitars";
    p.source.type        = "multisampleFolder";
    p.source.path        = src;
    p.source.mappingMode = "hardZones";
    p.source.rootNotePattern = { "C", "D#", "F#", "A" };
    p.amp = { 0.0f, 0.0f, 5.0f, 250.0f, 0.85f, 350.0f };
    p.filter = { true, "lowpass", 16000.0f, 0.1f, 0.0f, 0.0f };
    p.main   = { true, 0.0f, 0.0f, 0, 0, 0.0f };
    p.layer2 = { false, -12.0f, 0.0f, 1, 0, -5.0f };
    p.chorus = { false, 0.4f, 0.25f, 0.0f };
    p.delay  = { false, 300.0f, 0.3f, 0.0f };
    p.reverb = { false, 0.5f, 0.4f, 0.0f };
    p.saturation = { false, 0.0f, 0.0f };
    p.lfo1 = { false, "filter.cutoffHz", "sine", 0.25f, 0.1f };
    p.lfo2 = { false, "amp.pan",         "sine", 0.12f, 0.15f };
    p.advanced = { 0.0f, 0.0f, 0.25f, 0.18f, 0.0f, 0.0f, 16 };
    return p;
}

juce::Array<UserPreset> buildGuitarBank(const juce::String& src)
{
    juce::Array<UserPreset> out;

    auto add = [&](UserPreset p) { out.add(p); };

    // 1. Dark Pain Guitar
    { auto p = makeBase("Dark Pain Guitar", src);
      p.amp.attackMs = 18; p.amp.releaseMs = 480;
      p.filter.cutoffHz = 2200; p.filter.resonance = 0.2f;
      p.saturation = { true, 0.18f, 0.25f };
      p.reverb = { true, 0.6f, 0.5f, 0.28f };
      add(p); }

    // 2. Wide Chorus Guitar
    { auto p = makeBase("Wide Chorus Guitar", src);
      p.filter.cutoffHz = 9000;
      p.chorus = { true, 0.45f, 0.55f, 0.45f };
      p.reverb = { true, 0.4f, 0.4f, 0.15f };
      add(p); }

    // 3. Clean Drill Guitar
    { auto p = makeBase("Clean Drill Guitar", src);
      p.amp.attackMs = 2; p.amp.releaseMs = 180;
      p.filter.cutoffHz = 14000;
      p.reverb = { true, 0.3f, 0.5f, 0.08f };
      add(p); }

    // 4. Sad Room Guitar
    { auto p = makeBase("Sad Room Guitar", src);
      p.amp.attackMs = 12; p.amp.releaseMs = 520;
      p.filter.cutoffHz = 3200;
      p.reverb = { true, 0.55f, 0.55f, 0.32f };
      add(p); }

    // 5. Reverse Dream Guitar
    { auto p = makeBase("Reverse Dream Guitar", src);
      p.amp.attackMs = 600; p.amp.releaseMs = 1200; p.amp.sustain = 0.95f;
      p.filter.cutoffHz = 6000;
      p.delay  = { true, 480.0f, 0.45f, 0.28f };
      p.reverb = { true, 0.75f, 0.4f, 0.40f };
      add(p); }

    // 6. Lo-Fi Tape Guitar
    { auto p = makeBase("Lo-Fi Tape Guitar", src);
      p.filter.cutoffHz = 3800; p.filter.resonance = 0.15f;
      p.saturation = { true, 0.35f, 0.4f };
      p.advanced.humanizePitchCents = 8.0f;
      p.reverb = { true, 0.4f, 0.7f, 0.18f };
      add(p); }

    // 7. Ambient Heaven Guitar
    { auto p = makeBase("Ambient Heaven Guitar", src);
      p.amp.attackMs = 350; p.amp.releaseMs = 2200; p.amp.sustain = 1.0f;
      p.filter.cutoffHz = 8500;
      p.chorus = { true, 0.3f, 0.4f, 0.25f };
      p.reverb = { true, 0.85f, 0.45f, 0.55f };
      add(p); }

    // 8. Evil Filter Guitar
    { auto p = makeBase("Evil Filter Guitar", src);
      p.filter.cutoffHz = 1400; p.filter.resonance = 0.65f; p.filter.drive = 0.35f;
      p.saturation = { true, 0.45f, 0.5f };
      p.reverb = { true, 0.5f, 0.4f, 0.22f };
      add(p); }

    // 9. Memphis Delay Guitar
    { auto p = makeBase("Memphis Delay Guitar", src);
      p.filter.cutoffHz = 4500;
      p.delay  = { true, 180.0f, 0.42f, 0.32f };
      p.reverb = { true, 0.4f, 0.5f, 0.15f };
      add(p); }

    // 10. Warm Vintage Guitar
    { auto p = makeBase("Warm Vintage Guitar", src);
      p.filter.cutoffHz = 5500;
      p.saturation = { true, 0.22f, 0.35f };
      p.reverb = { true, 0.45f, 0.55f, 0.20f };
      add(p); }

    // 11. Distant Reverb Guitar
    { auto p = makeBase("Distant Reverb Guitar", src);
      p.amp.gainDb = -4;
      p.filter.cutoffHz = 4200;
      p.reverb = { true, 0.85f, 0.5f, 0.55f };
      add(p); }

    // 12. Bright Pop Guitar
    { auto p = makeBase("Bright Pop Guitar", src);
      p.amp.attackMs = 2; p.amp.releaseMs = 280;
      p.filter.cutoffHz = 15000;
      p.chorus = { true, 0.5f, 0.25f, 0.20f };
      p.reverb = { true, 0.35f, 0.4f, 0.12f };
      add(p); }

    // 13. Detuned Haunted Guitar
    { auto p = makeBase("Detuned Haunted Guitar", src);
      p.filter.cutoffHz = 3600;
      p.layer2 = { true, -6.0f, 0.0f, 0, 0, -12.0f };
      p.chorus = { true, 0.25f, 0.45f, 0.35f };
      p.reverb = { true, 0.55f, 0.5f, 0.28f };
      add(p); }

    // 14. Soft Velvet Guitar
    { auto p = makeBase("Soft Velvet Guitar", src);
      p.amp.attackMs = 35; p.amp.releaseMs = 650;
      p.filter.cutoffHz = 4800;
      p.reverb = { true, 0.55f, 0.6f, 0.22f };
      add(p); }

    // 15. Hard Pick Guitar
    { auto p = makeBase("Hard Pick Guitar", src);
      p.amp.attackMs = 1; p.amp.releaseMs = 150;
      p.filter.cutoffHz = 13000;
      p.saturation = { true, 0.18f, 0.3f };
      add(p); }

    // 16. Floating Space Guitar
    { auto p = makeBase("Floating Space Guitar", src);
      p.amp.attackMs = 220; p.amp.releaseMs = 1500;
      p.filter.cutoffHz = 7800;
      p.chorus = { true, 0.35f, 0.45f, 0.30f };
      p.delay  = { true, 520.0f, 0.5f,  0.25f };
      p.reverb = { true, 0.82f, 0.4f,  0.45f };
      p.lfo2 = { true, "amp.pan", "sine", 0.18f, 0.45f };
      add(p); }

    // 17. Chicago Pain Guitar
    { auto p = makeBase("Chicago Pain Guitar", src);
      p.amp.attackMs = 8; p.amp.releaseMs = 420;
      p.filter.cutoffHz = 3800;
      p.saturation = { true, 0.15f, 0.25f };
      p.delay  = { true, 320.0f, 0.30f, 0.18f };
      p.reverb = { true, 0.55f, 0.5f,  0.25f };
      add(p); }

    // 18. Muted Trap Guitar
    { auto p = makeBase("Muted Trap Guitar", src);
      p.amp.attackMs = 1; p.amp.releaseMs = 120;
      p.filter.cutoffHz = 3200;
      p.reverb = { true, 0.25f, 0.5f, 0.08f };
      add(p); }

    // 19. Cinematic Guitar Pad
    { auto p = makeBase("Cinematic Guitar Pad", src);
      p.amp.attackMs = 800; p.amp.releaseMs = 2800; p.amp.sustain = 1.0f;
      p.filter.cutoffHz = 6500;
      p.layer2 = { true, -10.0f, 0.0f, 1, 0, 0.0f };
      p.reverb = { true, 0.9f, 0.45f, 0.6f };
      add(p); }

    // 20. Broken Radio Guitar
    { auto p = makeBase("Broken Radio Guitar", src);
      p.filter.type = "bandpass"; p.filter.cutoffHz = 1800; p.filter.resonance = 0.5f;
      p.saturation = { true, 0.4f, 0.5f };
      p.reverb = { true, 0.25f, 0.6f, 0.10f };
      add(p); }

    return out;
}

}} // namespace dida::userpreset
