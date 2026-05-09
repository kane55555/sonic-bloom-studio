#!/usr/bin/env python3
"""
validate_presets.py — Validate every .didasynthpreset under a directory
against the v2 schema. Prints a summary and exits non-zero on errors.

Usage:
    python validate_presets.py                # studio_root()/Presets
    python validate_presets.py path/to/dir
"""
from __future__ import annotations
import argparse, json, sys
from pathlib import Path
from dida_common import studio_root

REQUIRED_TOP = {"schemaVersion","plugin","presetId","name","bank","category",
                "engine","layers","globalFilter","effects","macros","quality"}
ALLOWED_LAYER_TYPES = {"sample","oscillator","noise","texture"}
ALLOWED_CATEGORIES = {
    "DrillBells","AlienLeads","PainPianos","ChoirsVox","Guitars","DarkPads",
    "Plucks","Bass808","FXRisers","Textures","Uncategorized",
}

def validate(p: dict) -> list[str]:
    errs = []
    miss = REQUIRED_TOP - set(p.keys())
    if miss: errs.append(f"missing keys: {sorted(miss)}")
    if p.get("plugin") != "DIDITAGAIN STUDIO":
        errs.append("plugin must be 'DIDITAGAIN STUDIO'")
    if p.get("category") not in ALLOWED_CATEGORIES:
        errs.append(f"invalid category: {p.get('category')}")
    layers = p.get("layers")
    if not isinstance(layers, list) or not (1 <= len(layers) <= 4):
        errs.append("layers must be 1..4 entries")
    else:
        for i, l in enumerate(layers):
            if not isinstance(l, dict):
                errs.append(f"layer[{i}] not object"); continue
            if l.get("type") not in ALLOWED_LAYER_TYPES:
                errs.append(f"layer[{i}].type invalid: {l.get('type')}")
            for k in ("id","enabled","volume","pan","ampEnvelope"):
                if k not in l: errs.append(f"layer[{i}] missing {k}")
            if l.get("type") == "sample":
                if "rootMidi" not in l: errs.append(f"layer[{i}] sample missing rootMidi")
    return errs

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("path", nargs="?", type=Path, default=None)
    args = ap.parse_args()
    base = args.path or (studio_root() / "Presets")
    if not base.exists():
        print(f"no such path: {base}"); return 2

    files = sorted(base.rglob("*.didasynthpreset"))
    print(f"Validating {len(files)} preset(s) under {base}")
    failed = 0
    for f in files:
        try:
            p = json.loads(f.read_text(encoding="utf-8"))
        except Exception as e:
            print(f"  FAIL {f.relative_to(base)}: cannot parse json: {e}")
            failed += 1; continue
        errs = validate(p)
        if errs:
            failed += 1
            print(f"  FAIL {f.relative_to(base)}")
            for e in errs: print(f"     - {e}")
        else:
            print(f"  OK   {f.relative_to(base)}")
    print(f"Done. ok={len(files)-failed}  failed={failed}")
    return 0 if failed == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
