# DIDITAGAIN STUDIO - Native Plugin v2

Clean rebuild of the native JUCE plugin, beside the legacy `native/plugin/`.
The legacy folder is intentionally left untouched.

## Phase 1 (this folder)

Smallest stable JUCE VST3 / Standalone:

- Empty synth processor (clears buffer, accepts MIDI).
- Dark editor window with the "DIDITAGAIN STUDIO" title.
- No browser, no sampler, no crop tab, no FX chain, no hybrid layers.
- Only standard JUCE modules. No `juce_audio_processors_headless`.
- One target: `DIDITAGAIN_STUDIO_V2` (VST3 + Standalone).

## Build

```bash
cmake -S native/plugin_v2 -B native/plugin_v2/build -DJUCE_DIR=<path-to-JUCE>
cmake --build native/plugin_v2/build --config Release --target DIDITAGAIN_STUDIO_V2_VST3
```

`JUCE_DIR` defaults to `../JUCE` or `C:/JUCE` if available.

## Roadmap (do NOT implement yet)

- Phase 2: basic synth voice
- Phase 3: sample playback across keys
- Phase 4: preset browser
- Phase 5: audio crop / loop tab
- Phase 6: hybrid layers and FX
