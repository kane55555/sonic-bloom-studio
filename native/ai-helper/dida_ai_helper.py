#!/usr/bin/env python3
"""dida_ai_helper.py — AI Texture v0.1 offline orchestration (STUB).

v0.1 is CACHED, not realtime. This helper runs OFFLINE only. It ties together
DDSP timbre analysis (ddsp_profile.py) and texture pack export
(export_texture_pack.py). The plugin never imports or runs any of this — it only
consumes the cached WAV textures these scripts produce.

Usage (stub):
    python dida_ai_helper.py analyze  <input.wav> [--out profile.json]
    python dida_ai_helper.py pack     <textures_dir> <out_dir>
"""
from __future__ import annotations
import argparse
import json
import sys
from pathlib import Path

from ddsp_profile import analyze_timbre
from export_texture_pack import export_pack


def cmd_analyze(args: argparse.Namespace) -> int:
    profile = analyze_timbre(Path(args.input))
    out = Path(args.out) if args.out else Path(args.input).with_suffix(".ddsp.json")
    out.write_text(json.dumps(profile, indent=2))
    print(f"[dida-ai] wrote DDSP profile -> {out}")
    return 0


def cmd_pack(args: argparse.Namespace) -> int:
    manifest = export_pack(Path(args.textures_dir), Path(args.out_dir))
    print(f"[dida-ai] exported texture pack: {len(manifest.get('textures', []))} textures")
    return 0


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="DIDITAGAIN AI Texture v0.1 helper (offline, cached)")
    sub = p.add_subparsers(dest="cmd", required=True)

    a = sub.add_parser("analyze", help="DDSP timbre/pitch analysis")
    a.add_argument("input")
    a.add_argument("--out", default=None)
    a.set_defaults(func=cmd_analyze)

    k = sub.add_parser("pack", help="Bundle cached WAV textures into a pack")
    k.add_argument("textures_dir")
    k.add_argument("out_dir")
    k.set_defaults(func=cmd_pack)

    args = p.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
