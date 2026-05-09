# Current Audio Architecture

## Voice graph (per `SynthVoice`)

```
Sample (Layer 1, multisample, optionally looping)  --+
Osc B  (Layer 2, body / shimmer)                   --+--> Filter --> Amp Env --> Voice out
Sub Osc(Layer 4, sine sub or shimmer)              --+
Noise  (Layer 3, white/pink)                       --+
```

## Engine -> FX

`SynthEngine::renderBlockWithFx` renders all voices then runs the global
`FxChain` (saturation, chorus, delay, reverb, EQ, compressor, limiter,
master gain).

## Preset -> APVTS -> Voice

V2 presets flow through `HybridPresetApplier` which writes APVTS
parameters. `PluginProcessor::processBlock` reads APVTS and calls
`forEachSynthVoice` to push the snapshot into each voice. This means
adjusting any APVTS-bound knob, automating a parameter, or running the
macro mapper takes effect on the next block.

## Macros

8 APVTS macros (`macro1`..`macro8`). `MacroMapper` resolves V2
`macros[].targets[].path` strings to APVTS IDs. When a preset declares
targets, the mapper drives those params and the legacy hardcoded macro
behavior is skipped to avoid double modulation.

## Files of interest

- `DSP/Voice.{h,cpp}`           — sampler + osc + noise + filter + envs.
- `DSP/SynthEngine.{h,cpp}`     — voice pool, sample source, looping.
- `DSP/FxChain.{h,cpp}`         — global FX bus.
- `Presets/PresetManager.{h,cpp}` — load/save, routes V2 to applier.
- `Presets/HybridPresetApplier.{h,cpp}` — V2 -> engine mapping.
- `Presets/MacroMapper.{h,cpp}` — V2 macro target resolution.
- `Presets/PresetTemplateFactory.cpp` — per-category default presets.
- `PluginProcessor.cpp` — APVTS -> voice + FX, looping decision.
