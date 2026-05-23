# Vintage Analog Engine Upgrade

Transform the active plugin (`native/plugin`) from a basic subtractive synth into a vintage-inspired analog modeling engine, while preserving APVTS parameter IDs, MIDI playback, preset stepping, and VST3 build stability.

## Scope

This is a deep DSP upgrade across voice, oscillator, filter, envelope, modulation, chorus, and preset layers. All changes land in `native/plugin/Source/DSP` and `native/plugin/Source/Presets`. Legacy files in `plugin_legacy_broken` are referenced for ideas only — nothing is wholesale restored.

## Architecture overview

```text
[Per-voice]
 OscA + OscB + Sub + Noise (PolyBLEP, drift, phase rand)
        |
        v
   Osc Mixer  --> Pre-filter Drive --> Filter (LP12/LP24/HPF/BP, dual)
        |                                       |
        |                                       v
        |                               Post-filter Saturation
        |                                       |
        v                                       v
                                   VCA (RC-curve amp env)
                                                |
[Global FX chain]                               v
 BBD Chorus -> Delay -> Reverb -> EQ -> Glue Comp -> Limiter

[Voice cards]
 8 persistent calibration profiles scaled by global "Vintage" amount.
```

## Implementation steps

1. **VoiceCard system** — new `DSP/VoiceCard.h` holding 8 persistent calibration profiles (pitch/PW/gain/cutoff/res/env/VCA/pan offsets). Global `vintageAmount` (0–1) scales all offsets. `SynthEngine` assigns a card index round-robin to each `SynthVoice`. New APVTS param `vintage_amount` (additive, doesn't break existing IDs).

2. **Oscillator upgrade** (`DSP/Oscillator.h`) — PolyBLEP saw/square/pulse/triangle, sine, noise. Phase randomization at note-on. Per-voice slow drift LFO (0.03–0.25 Hz). DCO vs VCO mode toggle. PWM input from LFO+env. Hard sync and cross-mod hooks for OscB. Sub osc square one octave down (already present, retune).

3. **Filter upgrade** (`DSP/FilterBlock.h`) — LP12/LP24/HPF/BP modes. Juno-style musical saturation in the ladder. CS-style dual HPF→LPF mode. Pre-filter drive + post-filter soft sat (tanh). Smoothed cutoff/res via one-pole to kill zipper. Key tracking + velocity-to-cutoff inputs. Bipolar env amount.

4. **Envelopes** (`DSP/Envelope.h`) — RC-curve analog stages (exponential). Click-free fast attack via min-time clamp + smoothing. Retrigger modes: reset / legato / analog-partial. Per-voice timing scaled by card offsets. Velocity-to-attack scaling.

5. **Modulation matrix** (`DSP/ModMatrix.h`) — Fixed-route matrix: LFO1→pitch/cutoff/PW, LFO2→pan, ENV1→cutoff/pitch, ENV2→amp, vel→amp/cutoff/envAmt, modWheel→vibrato, aftertouch→cutoff/vibrato. Evaluated once per block in `SynthVoice::renderNextBlock`.

6. **BBD chorus** (`DSP/ChorusBlock.h`) — Replace `juce::dsp::Chorus` with two modulated delay lines (6–18 ms base), darkened wet (LP ~6 kHz), optional noise floor, stereo LFO phase offset, modes I / II / I+II. Mono-safe via mid/side handling.

7. **Gain staging** — Add per-stage compensation constants in `Voice` and `FxChain` so signal hits each stage at ~−12 dBFS RMS. Document with comments.

8. **Category EQ curves** — Extend `HybridPresetApplier` with `applyToneCurveForCategory` (Bass/Brass/Pads/Keys/Leads) wiring into existing `EQBlock` + `setReverbInputHighPass*` helpers.

9. **Vintage Synth factory bank** — New `Presets/VintageSynthBank.cpp/.h` with 20 sample-free presets (Juno pad, Jupiter brass, Prophet keys, CS pad, analog bass, mono lead, PWM pad, string machine, pluck, bell, dark choir, Reese, soft poly keys, horror lead, tape pad, analog init, +4 fillers). Registered in `PresetManager` under a "Vintage Synth" category. Each preset sets engine mode, osc waveforms, filter mode, env shapes, mod routes, chorus mode.

10. **Wire-up & safety** — `PluginProcessor`: register `vintage_amount` and any new IDs additively; attach listeners. `SynthEngine`: distribute voice cards; route new setters to voices. Confirm preset stepping during MIDI playback still uses existing `canSafelyMutateVoices` guard. CMake auto-globs new files.

## Technical notes

- **APVTS stability**: only *add* parameter IDs; never rename/remove. New IDs: `vintage_amount`, `osc_mode` (DCO/VCO), `filter_mode` (LP12/LP24/HPF/BP/Dual), `chorus_mode` (I/II/I+II), `env_retrigger`.
- **Voice card persistence**: card table generated once at engine construction with a fixed seed so the same card always sounds the same across sessions.
- **PolyBLEP**: standard 2-sample correction; avoids extra latency.
- **Filter smoothing**: 5 ms one-pole on cutoff/res/envAmount.
- **Legacy migration**: only `LFO.h` shape ideas and ModMatrix structure are referenced from `plugin_legacy_broken`; no files copied verbatim — all rewritten with comments noting origin.
- **Build**: header-heavy DSP keeps CMake source globs happy; new `.cpp` stubs added where the existing pattern requires them (matches `LayerBusProcessor.cpp` precedent).
- **No removals**: all current setters on `FxChain`, `Voice`, `SynthEngine` remain; new behavior is opt-in via new parameters defaulting to mild vintage (0.25).

## Validation

- Build VST3 cleanly (`DIDITAGAIN_STUDIO_VST3` target).
- Smoke-load each new factory preset and confirm no NaNs / silence.
- Confirm preset switching mid-MIDI-playback still honors `canSafelyMutateVoices`.
- A/B a chord on a Juno pad preset: voices should detune slightly, chorus should warble BBD-style, filter should saturate without harshness.
