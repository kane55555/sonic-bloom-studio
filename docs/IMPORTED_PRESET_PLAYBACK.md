# Imported Preset Playback

When you import a sample (one-shot or loop) via the plugin's Import panel,
DIDITAGAIN STUDIO does **not** treat it as "a sampler with one zone." It
generates a full hybrid V2 preset (`HybridPresetV2`) using
`PresetTemplateFactory` for the chosen category, and the imported audio
becomes Layer 1 of that preset.

## Pipeline

1. `ImportReviewPanel` copies the WAV into
   `Documents/DIDITAGAIN STUDIO/Samples/Imported/...`.
2. `HybridPresetGenerator::generate` builds a V2 preset from the category
   template, swapping in the imported file as Layer 1 (`source`,
   `rootMidi`, `rootNote`, `pitchTracking`, `oneShotMode`).
   Layers 2-4 keep the template's volumes/envelopes — they are **not**
   zeroed out.
3. `PresetManager::loadPresetFromFile` detects `schemaVersion == 2.0.0`
   and delegates to `HybridPresetApplier`.
4. `HybridPresetApplier::apply` writes APVTS parameters for every layer
   (Osc B, Sub, Noise), the global filter, FX, and decides looping via
   `shouldLoopForCategory`.
5. `MacroMapper::buildFrom` resolves any V2 macro target paths to APVTS
   parameter IDs.
6. `PluginProcessor::processBlock` swaps the sample source, sets
   per-block looping, calls `MacroMapper::apply`, and skips the legacy
   hardcoded macro behavior when preset macros are active.

## Looping rules

`HybridPresetApplier::shouldLoopForCategory(category, oneShotMode, layerLoop)`:

| Category                                  | Loops by default?    |
|-------------------------------------------|----------------------|
| FXRisers                                  | no (one-shot)        |
| Bass808                                   | no, unless layerLoop |
| DrillBells / Plucks / PainPianos / Guitars| no                   |
| ChoirsVox                                 | yes                  |
| AlienLeads                                | yes                  |
| DarkPads / Textures                       | yes                  |
| any preset with `oneShotMode = true`      | no (overrides)       |
| any preset with `layer.loop = true`       | yes (overrides)      |

## What's wired

- Layer 1 sample source, root MIDI, looping, pitch tracking, one-shot.
- Layer 2 -> Osc B (waveform, level, octave/semi/detune).
- Layer 3 -> Noise level.
- Layer 4 -> Sub osc (enabled + level).
- Global filter type/cutoff/resonance/drive.
- FX: reverb mix/size, delay mix/feedback, chorus mix, distortion drive.
- Per-category mono mode + glide (Bass808).
- Macro target paths -> APVTS params.
