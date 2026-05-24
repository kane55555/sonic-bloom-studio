# Multi-Engine Hybrid Synth Upgrade

Transform DIDITAGAIN STUDIO into a true multi-engine workstation while keeping the existing PCM/multisample pipeline and all `.diapreset` files working unchanged.

## Architecture

```text
SynthVoice
  └── Partial[0..3]   (each independently enable-able)
        ├── engineType: pcm | analog | supersaw | fm | wavetable | granular
        ├── EngineSource  (polymorphic, see below)
        ├── FilterModel   (Clean/Analog/Vintage/Ladder/JP/Tape)
        ├── AmpEnv + LFO
        ├── ModMatrix slots
        └── pan / level / enable
  └── Shared: VoiceCharacter (drift, jitter), Unison, Spread, Exciter
  └── Global: FxChain (existing)
```

Engines all implement a tiny interface:
```cpp
struct IEngineSource {
    virtual void prepare(double sr, int blockSize) = 0;
    virtual void noteOn(int midi, float vel) = 0;
    virtual void noteOff() = 0;
    virtual void renderAdd(float* L, float* R, int n,
                           float pitchHz, const ModSnapshot&) = 0;
    virtual void reset() = 0;
};
```

## Files to create

Under `native/plugin/Source/DSP/Engines/`:

- `IEngineSource.h` — shared interface + `ModSnapshot` struct.
- `PcmEngine.h/.cpp` — thin wrapper over existing multisample voice playback so the current pipeline becomes "engineType: pcm". No behavior change.
- `AnalogEngine.h/.cpp` — saw/square/pulse/tri/sine/noise + PWM + sub osc + detune, drift, phase randomization. Reuses `UnisonEngine` for 2–8 voices.
- `SupersawEngine.h/.cpp` — 3–9 detuned saws with detune curve, spread, per-voice phase rand, drift.
- `FmEngine.h/.cpp` — 4-operator FM with algorithms (stack, pair, parallel, bell), per-op env, feedback on op1.
- `WavetableEngine.h/.cpp` — single wavetable osc with frame position + morph, unison, warp placeholder (linear/bend), mod-target hooks.
- `GranularEngine.h/.cpp` — placeholder grain player over a source buffer: grain size, density, position rand, pitch spread, stereo spread. Falls back to silence if no source.

Under `native/plugin/Source/DSP/`:

- `Partial.h/.cpp` — owns one `IEngineSource`, a `FilterModels` instance, amp env, LFO, mod-matrix slots, pan/level/enable. Renders into a stereo accumulator.
- Update `Voice.h/.cpp` — replace single render path with up to 4 partials accumulating into the existing post chain (exciter, spread, fx).

## Preset schema (backwards compatible)

Extend `UserPresetFormat.h`:

```cpp
struct PartialBlock {
    bool enabled = false;
    juce::String engineType = "pcm";   // pcm|analog|supersaw|fm|wavetable|granular
    float level = 1.0f, pan = 0.0f;
    int   pitchSemis = 0; float fineCents = 0.0f;
    // per-engine params kept as juce::var bag for forward-compat
    juce::var engineParams;
    FilterBlockData filter;
    AmpBlock        amp;
    LfoBlock        lfo;
    juce::Array<ModMatrixEntry> mods;
};

struct UserPreset {
    // ...existing fields unchanged...
    juce::String engineType;   // optional, default "pcm"
    juce::Array<PartialBlock> partials;   // optional, up to 4
};
```

Rules for backwards compat in `UserPresetLoader::fromJson`:
- Missing `engineType` and missing `partials` → behave exactly as today (synthesize a single implicit PCM partial from existing amp/filter/main/layer2 fields).
- If `partials` present, those drive `Voice` and the legacy main/layer2 blocks are still applied to partial[0]/[1] for compatibility.
- Unknown `engineType` values fall back to `pcm` with a logged warning.

`UserPresetLoader::applyUserPreset` translates partials → engine instances on the voice. Logs `[Preset] <name> engines: [pcm, analog]` etc.

## Filter upgrades

Extend `DSP/Synthesis/FilterModels.h`:
- Add `Mode::Clean/Analog/Vintage/Ladder/JP/Tape`.
- Each mode = SVF core + per-mode drive curve, soft-clip in feedback path, and resonance shaping.
- Add `keytrack`, `envAmount` inputs that the partial feeds in.

## Modulation matrix

Reuse existing `ModMatrixEntry` schema. Wire these destinations at the partial level so every engine gets them:
- `filter.cutoff`, `filter.reso`, `amp.gain`, `amp.pan`, `osc.pitch`, `osc.pulseWidth`, `wt.position`, `fm.opXLevel`.

Sources: `env1`, `env2`, `lfo1`, `lfo2`, `velocity`, `modwheel`, `aftertouch`, `keytrack`. Resolved in `Partial::renderAdd` into a `ModSnapshot` passed to each engine per block.

## Voice character

Extend existing `AnalogDrift` + `VoiceRandomizer` so they expose:
- pitch instability (cents)
- cutoff jitter
- envelope time jitter
- per-voice pan offset

`Voice` owns one of each and feeds them into every partial's `ModSnapshot`.

## Preset categories

Just add the new category strings to `PresetIndex` / `UserPreset.category` parsing — no schema work needed. New banks (AnalogLeads/Pads, SupersawLeads, VintageKeys, FmBells, FmEPs, WavetableLeads/Pads, AmbientTextures) come in a follow-up; this task only ships the engine + 2–3 demo `.diapreset` files per new engine so the user can audition each engine. Existing categories untouched.

## CPU + safety

- Partials short-circuit when `enabled == false` (no allocations in audio thread).
- Wavetable + granular cap unison/grain counts.
- Reuse existing per-voice saturation + safety caps from the Vintage Synth pass.
- Debug log at preset load (rate-limited).

## Out of scope for this PR

- Full new factory preset banks for every new category (will follow once engines are validated).
- GUI partial editor — current MainSynthPanel keeps editing partial[0]; later PR adds a partial selector.
- Real granular DSP polish — ships as functional placeholder per spec.

## Verification

- Existing `.diapreset` files load unchanged (no `partials` key) and sound identical (PCM path).
- New presets with `"engineType": "analog"` etc. produce engine-specific sound.
- Debug log prints active engines per preset.
- Build passes; harness runs build automatically.
