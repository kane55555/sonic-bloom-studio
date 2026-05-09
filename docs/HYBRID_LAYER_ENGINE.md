# Hybrid Layer Engine

A preset has up to four layers. Each layer is one of:

| Type       | Source                              |
|------------|-------------------------------------|
| sample     | a `.wav/.flac/.aif` under `Samples/`|
| oscillator | sine/triangle/saw/square/pulse/supersaw |
| noise      | white or pink                       |
| texture    | a long looping/one-shot sample      |

## Per-layer parameters

- enabled, volume (0..1), pan (-1..1)
- amp envelope (ADSR seconds)
- optional filter (type, cutoff, resonance, drive)
- sample layers: root note, pitch tracking, one-shot, loop, reverse,
  pitch (semis), fineTune (cents), startOffset
- oscillator layers: waveform, pitch, fineTune

## Global

- `globalFilter` runs after layer summation
- `effects` chain: EQ → saturation → chorus → delay → reverb → width → limiter
- `macros[]` modulate any number of dotted paths into the preset

## Runtime mapping (current build)

The native `SynthVoice` plays Layer 1 sample sources directly through the
existing multisample renderer. Layers 2–4 (oscillator/noise) are read by
`PresetManager` and mapped onto the existing oscillator/FX parameters so
audio is produced. A future commit lands a true 4-layer mixer inside
`SynthVoice` so each layer renders independently with its own ADSR/filter.

A simple sine fallback in `SynthVoice::renderNextBlock` ensures non-sample
presets remain audible during preset switching.

## Macro evaluation

For each macro, every target's `path` is split on `.` and resolved against
the in-memory preset. The target value is `min + value * (max - min)`,
written back at audio-rate (smoothed) by the engine.

## Update — V2 applier + macro mapper

As of this pass, `HybridPresetV2` presets no longer collapse to a single
sample layer at load time. `Presets/HybridPresetApplier` maps every
layer (sample, osc body, noise, sub/shimmer), the global filter, FX,
and category-specific mono/glide settings onto APVTS parameters. The
imported sample is still Layer 1 and still plays chromatically across
the keyboard, but the support layers are audible by default — so an
imported bell becomes a "Drill Bell" instrument rather than a bare
sampler.

See `docs/IMPORTED_PRESET_PLAYBACK.md` for the full pipeline and the
per-category looping rules used by `shouldLoopForCategory`.
