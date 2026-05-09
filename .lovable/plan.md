## DIDITAGAIN STUDIO — UI Simplification + Crop/Loop Import Refactor

This is a multi-surface refactor: native JUCE plugin UI + DSP playback + React dashboard import editor + preset schema. To stay execution-ready I'm splitting it into 4 sequential phases, each independently shippable. Each phase ends in a buildable state.

### Logo
Your uploaded `DIDITAGAIN_5X5_FT_FINAL.pdf` is white logotype on black with a globe + arrow accent. I'll extract it as PNG, place it in:
- `native/plugin/Resources/diditagain_logo.png` (BinaryData → JUCE header)
- `src/assets/diditagain-logo.png` (React dashboard)
A clearly-named replacement slot stays so you can swap files without touching code.

---

### Phase 1 — Visual identity + UI simplification (native plugin only)

**Goal:** Plugin looks premium dark/teal, only Browser/Layers/FX visible, advanced stuff in a header menu, Cycle Test gone.

Files touched:
- `Source/UI/Theme.h` — switch accent from purple `#8B5CF6` to teal `#14F1D9`, refine charcoal surface tokens (`#0A0C10` bg, `#12161D` panel, `#1A2028` elevated). Single source of truth for all panels.
- `Source/PluginEditor.h/.cpp` — remove `tabSynth/tabMod/tabSettings/tabAccount/tabImport/modeToggle/cycleTest*` from the visible tab bar. Replace with a single `MenuButton` (☰) that opens a `juce::PopupMenu`:
  - Import One-Shot…
  - Advanced Sound Design (Synth)
  - Modulation
  - MIDI / Performance
  - Library / Settings
  - Account
  - About
  Each item shows the existing panel as a modal overlay (`addChildComponent` + dim background) so we don't lose features.
- New `Source/UI/HeaderBar.h` — logo (left), big preset name + category chip (center), master volume + menu (right).
- New `Source/UI/LogoComponent.h` — draws teal-tinted logo PNG via `juce::ImageCache`.
- `Source/UI/MacroPanel.h` — restyle 8 knobs with teal rings, larger labels.
- `Source/UI/LayerEditor.h` — collapse to 4 simplified rows (enable, vol, pan, pitch, source label, "Advanced ▾" expander that shows the existing dense controls).
- `Source/UI/FxPanel.h` — rack of EQ/Sat/Chorus/Delay/Reverb/Width/Limiter cards, each card: on/off + 1–3 knobs + chevron for full controls.
- `Source/Debug/PresetCycleTester.h` — left in tree, no UI hookup. (You said: don't delete the helper.)
- `CMakeLists.txt` — add logo resource via `juce_add_binary_data`.

### Phase 2 — Crop/loop metadata (schema + DSP playback)

**Goal:** Engine respects `cropStart/cropEnd/loopStart/loopEnd/loopCrossfadeMs/autoLoop/oneShotMode/pitchTracking`.

- `packages/preset-schema/src/presetTypes.ts` — add the fields you specified to the sample layer type and a `SampleImportMetadata` interface. Update validators + tests.
- `Source/Presets/HybridPresetV2.h` — mirror new fields.
- `Source/Presets/HybridPresetApplier.cpp` — write fields into APVTS/voice state.
- `Source/DSP/SampleLibrary.h/.cpp` — store per-sample crop/loop metadata.
- `Source/DSP/Voice.h/.cpp` — replace today's full-buffer wrap with a proper looper:
  - playback starts at `cropStart * length`
  - reads to `loopEnd`, then jumps back to `loopStart`
  - equal-power crossfade window of `loopCrossfadeMs` around the splice point (mix tail of pre-loop region with start of loop region)
  - `oneShotMode = true` skips loop entirely, lets envelope finish
  - `pitchTracking = false` ignores MIDI note ratio
- `Source/Presets/PresetTemplateFactory.cpp` + `HybridPresetGenerator.cpp` — apply per-category autoloop defaults exactly as you listed (DrillBells/PainPianos/Choirs/Guitars/DarkPads/AlienLeads → autoloop on; Bass808 → off unless detected; FXRisers → off; Plucks → off if <500ms; etc.).
- Default `loopStart = 0.2 * croppedLen`, `loopEnd = 0.95 * croppedLen`, `crossfade = 20ms` for melodic.

### Phase 3 — React import review with waveform crop editor

**Goal:** Functional crop/loop editor in the dashboard that writes the schema from Phase 2.

- `src/components/admin/PresetImportPanel.tsx` — extend with per-row "Edit ▾" that opens:
- New `src/components/admin/SampleCropEditor.tsx`:
  - Canvas waveform (decoded via WebAudio `AudioContext.decodeAudioData`)
  - Draggable handles: Start, End, Loop Start, Loop End (teal regions)
  - Numeric inputs as fallback (in seconds)
  - "Smooth Loop" slider 0–50ms
  - Toggles: Auto Loop, Play Across Keys, One-Shot Mode
  - "Preview" button that plays cropped + looped buffer in-browser using a small WebAudio looper for hold-to-preview
- Producer-friendly labels everywhere ("Start/End/Loop Start/Loop End/Smooth Loop/Auto Loop/Play Across Keys/One-Shot Mode").
- Finalize writes the new metadata into the preset JSON (Phase 2 schema).

### Phase 4 — Wire native ImportReviewPanel to new metadata

**Goal:** Native plugin import flow can also set crop/loop (minimum: numeric inputs + toggles; waveform optional, since drawing JUCE waveforms is heavier). Both surfaces produce identical preset JSON consumed by Phase 2 DSP.

- `Source/UI/ImportReviewPanel.h` — add Start/End/Loop Start/Loop End numeric fields, autoLoop/oneShot/pitchTracking toggles, "Smooth Loop" ms knob, Preview button (triggers a held middle-C note via the engine).
- `native/tools/import_samples.py` — also embeds defaults in generated metadata.

---

### Acceptance check after each phase
- After P1: open plugin → dark/teal, logo, 3 visible tabs, menu has the rest, no Cycle Test.
- After P2: a hand-edited preset JSON with `loopStart/loopEnd/crossfade` plays smoothly across keys.
- After P3: drop a WAV in the dashboard, set crop/loop, finalize → produces valid preset.
- After P4: same flow inside the plugin.

### What I'll NOT touch
- Preset/import architecture you already approved (HybridPresetApplier, MacroMapper, PresetTemplateFactory categories) — only extended.
- Browser/Layers/FX feature set — only restyled and reorganized.
- Existing factory presets — unchanged.

### Open question
Crop editor lives only in the React dashboard (Phase 3, full waveform UI), and the native plugin import gets numeric/toggle controls (Phase 4, no drawn waveform). If you want a drawn waveform inside the JUCE import panel too, that's an extra ~half-phase of work — say the word and I'll add it. Otherwise I'll proceed with the split above.

Approve and I'll start with Phase 1.