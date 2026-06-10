#!/usr/bin/env python3
"""export_texture_pack.py — Bundle cached WAV textures into a pack (STUB).

AI Texture v0.1: the plugin consumes CACHED .wav textures only. This offline
helper collects texture WAVs from a directory and writes a small manifest the
preset authoring tools can reference. (RAVE neural generation will feed this
step in a later version; for now it just packages pre-rendered WAVs.)
"""
from __future__ import annotations
import json
from pathlib import Path


def export_pack(textures_dir: Path, out_dir: Path) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    textures = []
    if textures_dir.is_dir():
        for wav in sorted(textures_dir.glob("*.wav")):
            textures.append({
                "name": wav.stem,
                "file": wav.name,
                "mode": "cached",
                "bytes": wav.stat().st_size if wav.exists() else 0,
            })

    manifest = {
        "schema": "dida.texturePack/v1",
        "version": "0.1",
        "mode": "cached",
        "note": "v0.1 cached textures only — no realtime neural inference.",
        "textures": textures,
    }
    (out_dir / "texture_pack.json").write_text(json.dumps(manifest, indent=2))
    return manifest


if __name__ == "__main__":
    import sys

    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("textures")
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("out")
    export_pack(src, dst)
    print(f"[dida-ai] wrote {dst / 'texture_pack.json'}")
