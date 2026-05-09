# DIDITAGAIN STUDIO — Native VST3 Synth Plugin

Production-grade JUCE/C++ source for the DIDITAGAIN STUDIO synthesizer.
Builds as a **VST3 instrument** (FL Studio compatible on Windows) and a
**Standalone** desktop app from the same codebase.

## Folder layout

```
native/plugin/
  CMakeLists.txt
  Source/
    PluginProcessor.{h,cpp}      — APVTS, parameter layout, processBlock
    PluginEditor.{h,cpp}         — Tabbed editor shell
    DSP/
      SynthEngine.{h,cpp}        — juce::Synthesiser host + FxChain
      Voice.{h,cpp}              — Per-voice DSP, glide, FM2, sub, noise
      Oscillator.{h,cpp}         — Sine/Tri/Saw/Sq/Pulse/SuperSaw/FM/WT
      FilterBlock.{h,cpp}        — SVF: LP12/24, HP12/24, BP, Notch + drive
      Envelope.{h,cpp}           — ADSR with stage tracking
      LFO.{h,cpp}                — Sine/Tri/Saw/Sq/S&H
      ModMatrix.{h,cpp}          — Up to 12 source→dest routings
      FxChain.{h,cpp}            — Saturation→Chorus→Delay→Reverb→Limiter
      Saturation.{h,cpp}         — Soft / Tape / Tube / HardClip
      ChorusBlock.{h,cpp}        — juce::dsp::Chorus wrapper
      DelayBlock.{h,cpp}         — Stereo + ping-pong, damping, fb-clamped
      ReverbBlock.{h,cpp}        — juce::Reverb wrapper
      UtilityDSP.{h,cpp}         — MIDI/dB helpers, smoother, pink noise
    Presets/
      PresetSchema.h             — JSON keys + version constant
      PresetManager.{h,cpp}      — Scan, validate, load→APVTS, save
      FactoryPresets.{h,cpp}     — 30 embedded presets, auto-extract
    UI/
      Theme.{h,cpp}              — Color tokens (charcoal/purple/teal)
      KnobLookAndFeel.{h,cpp}    — Custom rotary knob L&F
      MainSynthPanel.{h,cpp}     — Osc/Filter/Env/FX/Macro panel
      PresetBrowser.{h,cpp}      — Category + search browser
      BrowserComponent.{h,cpp}   — Underlying searchable list
    Licensing/
      LicenseClient.{h,cpp}      — Hardware fingerprint + JWT cache
      AccountState.{h,cpp}       — Plan / device / subscription DTO
  Presets/Factory/*.didasynthpreset  — 30 source-of-truth JSON presets
  Tests/
    PresetValidationTests.cpp    — JSON schema sanity check
    SynthSmokeTests.cpp          — Render audio, assert no NaNs
```

## Windows build (VST3 + Standalone)

Prerequisites:

- Visual Studio 2026 with the Desktop C++ workload
- CMake 3.22+
- Git
- JUCE 7.0.x cloned locally (e.g. `C:\JUCE`)

To avoid an MSVC 14.51 crash in JUCE's audio-format metadata templates, sample
import currently supports uncompressed 16-bit PCM `.wav` files directly and does
not link `juce_audio_formats`.

```bat
cd native\plugin
if exist build rmdir /s /q build
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DJUCE_DIR=C:/JUCE
cmake --build build --config Release --target DIDITAGAIN_STUDIO_VST3 -- /m:1
```

Output:
- VST3: `build/DIDITAGAIN_STUDIO_artefacts/Release/VST3/DIDITAGAIN STUDIO.vst3`
- Standalone: `build/DIDITAGAIN_STUDIO_artefacts/Release/Standalone/DIDITAGAIN STUDIO.exe`

`COPY_PLUGIN_AFTER_BUILD TRUE` in CMake will copy the VST3 to your system
plugin folder. In FL Studio: Options → Manage Plugins → Find more plugins.

## Preset format

`.didasynthpreset` files are JSON with the schema in `PresetSchema.h`.
Factory presets live in `native/plugin/Presets/Factory/` and are also
embedded into `FactoryPresets.cpp` so the binary is self-contained — on
first run they extract to the user's app data folder.

## Placeholders to upgrade later

The scaffold is playable as-is, but these areas are intentionally simple
and should be upgraded for shipping quality:

| Area               | Current                              | Recommended upgrade                                |
| ------------------ | ------------------------------------ | -------------------------------------------------- |
| Wavetable mode     | Two-partial sine stack               | Real wavetable bank with band-limited interpolation |
| FM4 mode           | Falls back to subtractive            | 4-operator FM with selectable algorithms            |
| HP24/LP24 filters  | Single-stage SVF                     | Cascade two SVFs or use Moore/ZDF ladder            |
| Mod matrix         | Engine present, not wired in voice   | Pull source values per-block, modulate destinations |
| Unison voices      | Per-osc setting, single voice render | Allocate N detuned sub-voices per note              |
| Reverb             | `juce::Reverb` (Freeverb)            | Algorithmic FDN or convolution                      |
| Delay tempo sync   | Free-time only                       | Use `getPlayHead()` BPM and divisions               |
| Pink noise         | 3-band approximation                 | Voss-McCartney with longer accumulator              |
| Output limiter     | `tanh` soft clip                     | True peak look-ahead limiter                        |
| Editor             | Tab shell                            | Wire `MainSynthPanel`/`PresetBrowser` per tab       |

## Testing

```bat
cmake --build build --target PresetValidationTests
build\Tests\Release\PresetValidationTests.exe
build\Tests\Release\SynthSmokeTests.exe
```

## License & code provenance

All DSP code in this folder is original implementation based on
publicly-known synthesis concepts (SVF, Chamberlin, ADSR, FM/PM, Freeverb).
No GPL code, no proprietary plugin source has been imported.
