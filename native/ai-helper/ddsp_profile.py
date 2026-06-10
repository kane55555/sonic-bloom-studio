#!/usr/bin/env python3
"""ddsp_profile.py — Offline DDSP-style timbre/pitch analysis (STUB).

AI Texture v0.1: this runs OFFLINE only and produces a normalized 0..1 timbre
profile that is stored in a preset's `ai.timbreProfile` block. The plugin never
runs this code.

The real implementation will use DDSP for spectral/loudness/pitch analysis.
This stub computes lightweight proxies (or fixed defaults) so the rest of the
pipeline is runnable end-to-end without heavy dependencies.
"""
from __future__ import annotations
import wave
from pathlib import Path


def _read_pcm_mono(path: Path):
    """Best-effort mono PCM read using the stdlib wave module. Returns [] on any
    problem (the analysis falls back to neutral defaults)."""
    try:
        with wave.open(str(path), "rb") as w:
            n = w.getnframes()
            ch = w.getnchannels()
            sw = w.getsampwidth()
            raw = w.readframes(min(n, w.getframerate() * 10))
        if sw != 2:
            return [], 0
        import array
        a = array.array("h")
        a.frombytes(raw)
        if ch > 1:
            a = a[0::ch]
        return [s / 32768.0 for s in a], w.getframerate() if False else 44100
    except Exception:
        return [], 0


def analyze_timbre(path: Path) -> dict:
    """Return an ai.timbreProfile-compatible dict (all 0..1) + metadata."""
    samples, _sr = _read_pcm_mono(path)

    if samples:
        peak = max((abs(s) for s in samples), default=0.0)
        rms = (sum(s * s for s in samples) / len(samples)) ** 0.5 if samples else 0.0
        # crude zero-crossing rate -> brightness proxy
        zc = sum(1 for i in range(1, len(samples)) if (samples[i - 1] < 0) != (samples[i] < 0))
        brightness = min(1.0, zc / max(1, len(samples)) * 8.0)
        noise_air = min(1.0, max(0.0, (peak - rms) * 1.5))
    else:
        brightness, noise_air = 0.5, 0.5

    return {
        "schema": "ddsp.profile/v1",
        "source": path.name,
        "provider": "ddsp",
        "profileVersion": 1,
        "textureMode": "cached",
        "rootMidi": 60,
        "timbreProfile": {
            "brightness": round(brightness, 3),
            "harmonicDensity": 0.5,
            "noiseAir": round(noise_air, 3),
            "attackNoise": 0.4,
            "pitchInstability": 0.05,
            "bodyWarmth": 0.5,
        },
    }


if __name__ == "__main__":
    import json
    import sys

    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("input.wav")
    print(json.dumps(analyze_timbre(src), indent=2))
