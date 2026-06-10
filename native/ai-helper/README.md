# DIDITAGAIN STUDIO — AI Helper (AI Texture v0.1)

> **v0.1 is CACHED, not realtime.** The plugin only ever plays back **cached
> `.wav` textures**. There is **no** TensorFlow / PyTorch / RAVE / ONNX inside
> the plugin. All neural work happens **offline** in these helper scripts.

## Pipeline

```text
  source audio  ──►  ddsp_profile.py   ──►  timbre/pitch analysis (DDSP)
                          │                  (brightness, harmonic density,
                          │                   noiseAir, attackNoise, etc.)
                          ▼
                   dida_ai_helper.py   ──►  orchestration / profile JSON
                          │
                          ▼  (future: RAVE neural texture generation)
                   export_texture_pack.py ─► cached .wav textures + .diapreset
                          │
                          ▼
                   DIDITAGAIN STUDIO plugin  ──► plays cached WAV textures only
```

### Stages

1. **DDSP (now, offline)** — `ddsp_profile.py` analyses a source sample and
   produces a normalized 0..1 timbre profile plus a detected root note. This
   profile is written into the preset's `ai.timbreProfile` block.
2. **RAVE (later, offline)** — a future step will use RAVE to *generate* neural
   texture audio from the DDSP profile. v0.1 does not ship this; it only
   documents the seam.
3. **Plugin (v0.1, realtime-safe)** — consumes the **cached** WAV texture via a
   `neuralTextureCached` partial. No model runs on the audio thread.

## Files

| File | Purpose |
|------|---------|
| `dida_ai_helper.py`   | CLI entry point / orchestration of the offline pipeline. |
| `ddsp_profile.py`     | Offline DDSP-style timbre + pitch analysis (stub). |
| `export_texture_pack.py` | Bundle cached WAV textures + `.diapreset` files. |

## Preset contract

A neural texture is referenced by a partial:

```json
{
  "engineType": "neuralTextureCached",
  "enabled": true,
  "followMainEnvelope": true,
  "eqRole": "neuralTexture",
  "engineParams": {
    "texturePath": "{DocsRoot}/Samples/AITextures/brass_air.wav",
    "loop": true,
    "rootMidi": 60,
    "pitchTracking": false,
    "levelDb": -18.0
  }
}
```

The optional `ai` block carries the offline analysis result:

```json
"ai": {
  "enabled": true,
  "provider": "ddsp",
  "profileVersion": 1,
  "textureMode": "cached",
  "analysisFile": "{DocsRoot}/Samples/AITextures/brass_air.ddsp.json",
  "timbreProfile": { "brightness": 0.6, "harmonicDensity": 0.5, ... }
}
```

Missing textures/analysis fail **silent** in the plugin and are flagged by the
preset-quality report (`AI_TEXTURE_MISSING`, `AI_ANALYSIS_MISSING`).
