## Goal

Keep the current "imported sample plays across the piano roll" behavior, but make every imported preset load as a full hybrid instrument (sample + body + air + shimmer/sub + per-category FX + working macros), not as a bare sampler.

This is a native (C++/JUCE) plugin refactor in `native/plugin/Source/`. Lovable web UI is unchanged except for documentation and an optional preset-edit panel (task 8) which I'll keep minimal.

## Scope (in order)

### 1. New: `Presets/HybridPresetApplier.{h,cpp}`
Single mapper from `HybridPresetV2` → engine. Owns all V2-to-engine translation that currently lives inline in `PresetManager.cpp`.

Responsibilities:
- Layer 1 (Sample): set sample source, root MIDI, `pitchTracking`, `oneShotMode`, per-layer loop, volume, envelope.
- Layer 2 (Oscillator body): waveform → `oscBWave`, pitch/fine → `oscBPitchOffsetSemis`/detune, volume → `oscBLevel`, enabled flag respected.
- Layer 3 (Noise/Air): noise type, volume → `noiseLevel`.
- Layer 4 (Shimmer/Sub): if category Bass808 → sub osc; else → second osc-style shimmer (waveform/pitch+12/detune via existing oscB or sub channel — see task 5).
- `globalFilter` → APVTS `filterCutoff`/`filterRes`/`filterType`.
- `effects.*` → APVTS reverb/delay/chorus/saturation/width/limiter parameters.
- Per-layer amp envelope → voice amp env (Layer 1 wins for the master amp env; support layers use lighter envs internally).
- Mono mode + glide for Bass808.
- Calls `synthEngine.setSampleSource`, `setSampleLooping(shouldLoopForCategory(...))`, `setFallbackSynthesisEnabled(false)` when a sample is present.
- Does **NOT** zero out support layers blindly. Replaces the current `oscBLevel = 0 / sub disabled / noiseLevel = 0` block in `PresetManager.cpp`.

### 2. `PresetManager.cpp`
- Detect `schemaVersion == "2.0.0"` → delegate to `HybridPresetApplier::apply(preset, apvts, synthEngine, macroMapper)`.
- V1 path untouched.

### 3. `PluginProcessor.cpp` — looping helper
Add free function:
```cpp
bool shouldLoopForCategory(const juce::String& category, bool oneShotMode, bool layerLoop);
```
Rules per spec (FXRisers/oneShotMode → false; DarkPads/Textures → true; layerLoop → true; ChoirsVox optional; bells/plucks/guitars/808 → false unless layerLoop). Replace the unconditional `setSampleLooping(true)` call.

### 4. `PresetTemplateFactory.cpp` + `HybridPresetGenerator.cpp` — keep support layers alive
- `PresetTemplateFactory::build` already produces category templates; extend it to fully populate Layers 2/3/4 with the per-category gain/pitch/waveform values from the spec (DrillBells, Bass808, ChoirsVox, PainPianos, AlienLeads, Plucks, DarkPads, Textures, FXRisers).
- `HybridPresetGenerator::generate`: **remove** the loop that disables/zeroes `layers[i>=1]`. Only override Layer 1 with imported sample data; trust the template for the rest.
- Set `oneShotMode`/`pitchTracking`/category-specific defaults from the template, not from generator overrides.

### 5. New: `Presets/MacroMapper.{h,cpp}`
- Parses `macros[].targets[].path` strings.
- Supported paths: `globalFilter.cutoff`, `effects.reverb.{mix,size}`, `effects.delay.{mix,feedback}`, `effects.saturation.drive`, `effects.chorus.mix`, `effects.width.amount`, `layers[N].volume`, `glideTime`, `monoMode`.
- Stores runtime `(apvtsParamID, min, max)` tuples per macro.
- Called from `PluginProcessor::processBlock` (or via APVTS listener on `macroN`) to push macro values onto target params.
- Falls back to current hardcoded macro1..8 mapping when a preset has no targets.

### 6. `DSP/Voice.{h,cpp}` — minor cleanup, no API break
- Add `RuntimeLayerState` struct + `setLayerState(int, const RuntimeLayerState&)` in `Voice.h`.
- Internally route to existing `oscBLevel` / `subLevel` / `noiseLevel` / sample fields. APVTS bridge stays. This is the "prepare for real per-layer rendering" hook the spec asks for; the audio path itself does not change in this pass beyond honoring per-layer volumes/pitch from the applier.

### 7. UI — `LayerEditor.h`, `MacroPanel.h`, simple/advanced split
- `MacroPanel`: keep 8 knobs but pull display names from current preset's macro names (fallback to defaults). Read names from a small `PresetManager` getter.
- `LayerEditor`: render 4 layer rows (Main Sample / Body / Air / Shimmer-Sub) showing enabled toggle, volume, pitch, waveform (osc layers) or sample source (sample layer), `oneShotMode`/`pitchTracking` toggles for Layer 1.
- Hide/remove any controls that aren't wired (per spec: don't show fake controls).

### 8. Imported-preset edit (minimal)
Add a "Preset Info" section in `LayerEditor` (or a small new panel) where the user can change category, root note, toggle pitchTracking/oneShotMode, enable/disable layers 2-4, adjust their volume, and save back to the `.didasynthpreset` JSON via `HybridPresetGenerator::toJsonString` + index refresh. Lovable dashboard counterpart deferred — native is the source of truth.

### 9. Docs
Update `docs/HYBRID_LAYER_ENGINE.md`; create `docs/CURRENT_AUDIO_ARCHITECTURE.md` and `docs/IMPORTED_PRESET_PLAYBACK.md` describing layer→DSP mapping, looping rules, macro target system, and what's wired vs planned.

## Out of scope for this pass
- Reworking the FX chain DSP itself.
- True multi-voice unison rendering inside a single `SynthVoice`.
- A full Lovable-side preset editor UI (web). Native edit panel covers task 8.

## Files touched

New:
- `native/plugin/Source/Presets/HybridPresetApplier.h` / `.cpp`
- `native/plugin/Source/Presets/MacroMapper.h` / `.cpp`
- `docs/CURRENT_AUDIO_ARCHITECTURE.md`
- `docs/IMPORTED_PRESET_PLAYBACK.md`

Edited:
- `native/plugin/Source/Presets/PresetManager.cpp` (delegate V2)
- `native/plugin/Source/Presets/PresetTemplateFactory.cpp` (full category templates)
- `native/plugin/Source/Presets/HybridPresetGenerator.cpp` (stop zeroing support layers)
- `native/plugin/Source/PluginProcessor.cpp` (looping helper, macro mapper hookup)
- `native/plugin/Source/DSP/Voice.h` / `.cpp` (`RuntimeLayerState`)
- `native/plugin/Source/DSP/SynthEngine.h` / `.cpp` (forward layer-state setters)
- `native/plugin/Source/UI/LayerEditor.h` (4-layer editor + preset info)
- `native/plugin/Source/UI/MacroPanel.h` (dynamic macro names)
- `native/plugin/CMakeLists.txt` (register new source files)
- `docs/HYBRID_LAYER_ENGINE.md`

## Verification
After each phase, the plugin must still compile (CMake target). I'll run a compile check at the end. I cannot run audio in the sandbox, so the four acceptance tests (Test A–D) are for you to perform in FL Studio after the build.

## Confirm before I start
This is a multi-hour native refactor across ~12 files. I'll do it in one pass without further questions unless something is genuinely ambiguous. Reply "go" (or with edits) and I'll execute.