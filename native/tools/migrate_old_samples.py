#!/usr/bin/env python3
"""
migrate_old_samples.py — Walk the legacy `Samples/<Instrument>/` layout and
re-route the files into the new v2 layout (Samples/Imported/<NewCategory>/)
with generated hybrid presets and an updated index.

Usage:
    python migrate_old_samples.py            # default: studio_root()/Samples
    python migrate_old_samples.py --dry-run
    python migrate_old_samples.py --root /path/to/StudioRoot
"""
from __future__ import annotations
import argparse, json, shutil, sys
from pathlib import Path

from dida_common import (
    studio_root, ensure_layout, LEGACY_CATEGORY_MAP,
    detect_note, detect_velocity, default_root_for,
    build_preset, load_index, save_index, upsert_index, now_iso,
)

LEGACY_DIR_KEYS = set(LEGACY_CATEGORY_MAP.keys())
AUDIO_EXTS = {".wav",".flac",".ogg",".mp3",".aif",".aiff"}

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=None)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    root = args.root or studio_root()
    layout = ensure_layout(root)
    legacy_root = layout["samples"]
    print(f"Migrating legacy samples under: {legacy_root}")

    moved = 0; skipped = 0
    index = load_index(layout["index"])

    for sub in sorted(legacy_root.iterdir()) if legacy_root.exists() else []:
        if not sub.is_dir(): continue
        key = sub.name.lower().replace(" ", "")
        # Skip the new layout's own subfolders.
        if sub.name in ("Imported","Factory","User"): continue
        if key not in LEGACY_DIR_KEYS:
            print(f"  - skipping unknown legacy folder: {sub.name}")
            continue
        new_cat = LEGACY_CATEGORY_MAP[key]
        for f in sub.rglob("*"):
            if not f.is_file() or f.suffix.lower() not in AUDIO_EXTS: continue
            stem = f.stem
            note = detect_note(stem)
            if note and len(note[0]) > 1 and note[0][-1].isdigit():
                note_token, midi = note
                root_source = "filename"
                pitch_track = new_cat not in ("FXRisers","Textures")
                needs_review = False
            else:
                note_token, midi, pitch_track = default_root_for(new_cat)
                root_source = "guessed"
                needs_review = True
            vel = detect_velocity(stem)
            safe = f"{new_cat}_{note_token}" + (f"_v{vel}" if vel is not None else "")
            sample_rel = f"Samples/Imported/{new_cat}/{safe}{f.suffix.lower()}"
            preset_rel = f"Presets/User/{new_cat}/{safe}.didasynthpreset"
            meta_rel   = f"Metadata/Imports/{safe}.import.json"

            dst_sample = root / sample_rel
            dst_preset = root / preset_rel
            dst_meta   = root / meta_rel

            if args.dry_run:
                print(f"  DRY  {sub.name}/{f.name}  ->  {sample_rel}")
                continue

            dst_sample.parent.mkdir(parents=True, exist_ok=True)
            dst_preset.parent.mkdir(parents=True, exist_ok=True)
            dst_meta.parent.mkdir(parents=True, exist_ok=True)

            if not dst_sample.exists():
                shutil.copy2(f, dst_sample)

            preset = build_preset(
                name=safe.replace("_"," "),
                category=new_cat,
                sample_path_rel=sample_rel,
                metadata_path_rel=meta_rel,
                original_filename=f.name,
                root_note=note_token,
                root_midi=midi,
                root_note_source=root_source,
                pitch_tracking=pitch_track,
                needs_review=needs_review,
                tags=[new_cat.lower(),"migrated","hybrid"],
            )
            dst_preset.write_text(json.dumps(preset, indent=2), encoding="utf-8")
            dst_meta.write_text(json.dumps({
                "originalPath": str(f),
                "migratedAt": now_iso(),
                "fromLegacyFolder": sub.name,
            }, indent=2), encoding="utf-8")

            index = upsert_index(index, {
                "presetId": preset["presetId"],
                "name": preset["name"],
                "bank": "User",
                "category": new_cat,
                "tags": preset["tags"],
                "presetPath": preset_rel,
                "samplePath": sample_rel,
                "createdAt": preset["dateCreated"],
                "modifiedAt": preset["dateModified"],
                "favorite": False,
                "userEdited": False,
                "needsReview": needs_review,
            })
            moved += 1
            print(f"  OK   {sub.name}/{f.name}  ->  {sample_rel}")

    if not args.dry_run:
        save_index(layout["index"], index)
    print(f"Done. migrated={moved} skipped={skipped}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
