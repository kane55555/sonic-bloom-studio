# Hybrid Preset Architecture — DIDITAGAIN STUDIO v2

## Overview

DIDITAGAIN STUDIO v2 is a **hybrid preset workstation**: 70% polished sample
workstation (Nexus-style browser + sample import) and 30% editable synth
engine (Zenology-style 4-layer patches with macros and global FX).

Every imported sound becomes:

1. A routed sample asset (`Samples/Imported/<Category>/...`)
2. A metadata file (`Metadata/Imports/*.import.json`)
3. A v2 hybrid preset (`Presets/User/<Category>/*.didasynthpreset`)
4. A searchable index entry (`Presets/index.json`)

## Categories

`DrillBells`, `AlienLeads`, `PainPianos`, `ChoirsVox`, `Guitars`,
`DarkPads`, `Plucks`, `Bass808`, `FXRisers`, `Textures`, `Uncategorized`.

Legacy folders (Brass, Piano, RhodesEP, 808Kit, …) are mapped automatically
via `LEGACY_CATEGORY_MAP` in `native/tools/dida_common.py`.

## Layer model

A preset has up to 4 layers (`sample`, `oscillator`, `noise`, `texture`).
Each layer carries: enabled, volume, pan, ADSR, optional filter. Sample
layers also carry root note, pitch tracking, one-shot, loop, reverse.
Layers sum into a global filter and a global FX chain (EQ, saturation,
chorus, delay, reverb, width, limiter).

## Macros

4 macros per preset, each value 0..1, each mapped to one or more dotted
target paths into the preset (e.g. `globalFilter.cutoff`, `effects.reverb.mix`).

## Backwards compatibility

- v1 `.didasynthpreset` files load via `PresetMigration::toV2()`.
- Old `Samples/<Instrument>/` folders work at runtime and can be upgraded
  with `python native/tools/migrate_old_samples.py`.
- See `IMPORT_PIPELINE.md` and `HYBRID_LAYER_ENGINE.md` for details.

## File map

| Concern              | Path |
|----------------------|------|
| TS schema + types    | `packages/preset-schema/src/` |
| Python importer      | `native/tools/import_samples.py` |
| Python migration     | `native/tools/migrate_old_samples.py` |
| C++ schema (v2)      | `native/plugin/Source/Presets/HybridPresetV2.h` |
| C++ migration        | `native/plugin/Source/Presets/PresetMigration.{h,cpp}` |
| C++ index            | `native/plugin/Source/Presets/PresetIndex.{h,cpp}` |
| C++ template factory | `native/plugin/Source/Presets/PresetTemplateFactory.{h,cpp}` |
| C++ generator        | `native/plugin/Source/Presets/HybridPresetGenerator.{h,cpp}` |
| Web admin            | `src/pages/PresetsPage.tsx` |
