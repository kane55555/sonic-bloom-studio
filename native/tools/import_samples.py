#!/usr/bin/env python3
"""
import_samples.py — Hybrid preset importer for DIDITAGAIN STUDIO.

For every audio file found, this tool:
  1. Classifies it into a producer-facing category (DrillBells, Bass808, ...).
  2. Detects (or guesses) a root note + optional velocity layer.
  3. Copies the audio under  Documents/DIDITAGAIN STUDIO/Samples/Imported/<Category>/.
  4. Writes a per-import metadata JSON under Metadata/Imports/.
  5. Generates a v2 .didasynthpreset under Presets/User/<Category>/.
  6. Upserts an entry into Presets/index.json.

CLI:
    python import_samples.py <file_or_folder> [...]
    python import_samples.py --inbox
    python import_samples.py --dry-run <files>
    python import_samples.py --review-json out.json <files>
    python import_samples.py --force <files>

`--review-json` writes the candidate list (no copies, no presets) so a UI
can present an Import Review screen and call this script again with edits.
"""
from __future__ import annotations
import argparse, json, shutil, sys
from pathlib import Path

from dida_common import (
    studio_root, ensure_layout, classify_category, detect_note, detect_velocity,
    default_root_for, build_preset, load_index, save_index, upsert_index, now_iso,
    display_name_for, next_preset_number,
)

AUDIO_EXTS = {".wav", ".flac", ".ogg", ".mp3", ".aif", ".aiff"}

def expand(paths: list[Path]) -> list[Path]:
    out: list[Path] = []
    for p in paths:
        if p.is_dir():
            out.extend(sorted(q for q in p.rglob("*") if q.is_file()))
        elif p.exists() and p.is_file():
            out.append(p)
    return [p for p in out if p.suffix.lower() in AUDIO_EXTS]

def make_candidate(src: Path, layout: dict) -> dict:
    stem = src.stem
    parent = src.parent.name
    category, cat_conf, _src = classify_category(stem, parent)

    # Note detection.
    note = detect_note(stem)
    if note and len(note[0]) > 1 and note[0][-1].isdigit():
        # filename had explicit octave
        note_token, midi = note
        root_source = "filename"
        pitch_track = category not in ("FXRisers","Textures")
        needs_review = False
    elif note:
        # letter only — use category default octave for the midi number,
        # but mark guessed.
        letter_token, semis_in_letter = note
        default_token, default_midi, pitch_track = default_root_for(category)
        # rebuild proper token using letter + default octave
        try:
            default_octave = int(default_token[1:]) if default_token[1].lstrip("-").isdigit() else 4
        except Exception:
            default_octave = 4
        note_token = f"{letter_token}{default_octave}"
        midi = (default_octave + 1) * 12 + semis_in_letter
        root_source = "guessed"
        needs_review = True
    else:
        note_token, midi, pitch_track = default_root_for(category)
        root_source = "guessed"
        needs_review = True

    velocity = detect_velocity(stem)

    one_shot = category in ("FXRisers",)
    # NOTE: presetName + target paths are assigned later via assign_auto_names()
    # so numbering ("Guitar 1", "Pad 2", ...) stays unique across the existing
    # index AND the current batch.
    return {
        "originalPath": str(src),
        "originalFileName": src.name,
        "detectedCategory": category,
        "categoryConfidence": cat_conf,
        "detectedRootNote": note_token,
        "rootMidi": midi,
        "rootNoteSource": root_source,
        "detectedVelocity": velocity,
        "targetSamplePath": "",
        "targetPresetPath": "",
        "targetMetadataPath": "",
        "template": category,
        "presetName": "",
        "tags": [category.lower(), "imported", "hybrid"],
        "oneShotMode": one_shot,
        "pitchTracking": pitch_track,
        "needsReview": needs_review,
        "warnings": [] if not needs_review else ["root note guessed"],
    }

def assign_auto_names(candidates: list[dict], existing_index: list[dict]) -> None:
    """Mutate candidates in place, giving each a producer-friendly name like
    "Guitar 1", "Pad 2", "Choir 3" — unique within its category."""
    reserved: dict[str, set[int]] = {}
    for c in candidates:
        cat = c["detectedCategory"]
        # Skip if caller (e.g. --from-review) already set a name + paths.
        if c.get("presetName") and c.get("targetPresetPath"):
            continue
        used = reserved.setdefault(cat, set())
        n = next_preset_number(existing_index, cat, reserved=used)
        used.add(n)
        display = display_name_for(cat)
        name = f"{display} {n}"
        safe_stem = name.replace(" ", "_")
        ext = Path(c["originalPath"]).suffix.lower()
        c["presetName"] = name
        c["targetSamplePath"]   = f"Samples/Imported/{cat}/{safe_stem}{ext}"
        c["targetPresetPath"]   = f"Presets/User/{cat}/{safe_stem}.didasynthpreset"
        c["targetMetadataPath"] = f"Metadata/Imports/{safe_stem}.import.json"

def _humanize(stem: str) -> str:
    return stem.replace("_", " ")

def finalize(c: dict, layout: dict, *, force: bool, dry: bool) -> str:
    src = Path(c["originalPath"])
    dst_sample = layout["root"] / c["targetSamplePath"]
    dst_preset = layout["root"] / c["targetPresetPath"]
    dst_meta   = layout["root"] / c["targetMetadataPath"]

    if dry:
        return f"DRY  {src.name}  ->  {c['detectedCategory']}/{dst_sample.name}  (preset: {dst_preset.name})"

    dst_sample.parent.mkdir(parents=True, exist_ok=True)
    dst_preset.parent.mkdir(parents=True, exist_ok=True)
    dst_meta.parent.mkdir(parents=True, exist_ok=True)

    if dst_sample.exists() and not force:
        i = 2
        while True:
            cand = dst_sample.with_name(f"{dst_sample.stem}_{i}{dst_sample.suffix}")
            if not cand.exists():
                dst_sample = cand
                # keep paths in metadata aligned
                rel = dst_sample.relative_to(layout["root"]).as_posix()
                c["targetSamplePath"] = rel
                break
            i += 1
    shutil.copy2(src, dst_sample)

    # Metadata
    meta = {
        "originalPath": str(src),
        "importedAt": now_iso(),
        **c,
    }
    dst_meta.write_text(json.dumps(meta, indent=2), encoding="utf-8")

    # Preset
    preset = build_preset(
        name=c["presetName"],
        category=c["detectedCategory"],
        sample_path_rel=c["targetSamplePath"],
        metadata_path_rel=c["targetMetadataPath"],
        original_filename=c["originalFileName"],
        root_note=c["detectedRootNote"],
        root_midi=c["rootMidi"],
        root_note_source=c["rootNoteSource"],
        pitch_tracking=c["pitchTracking"],
        one_shot=c["oneShotMode"],
        needs_review=c["needsReview"],
        tags=c["tags"],
    )
    dst_preset.write_text(json.dumps(preset, indent=2), encoding="utf-8")

    # Index
    index = load_index(layout["index"])
    entry = {
        "presetId": preset["presetId"],
        "name": preset["name"],
        "bank": preset["bank"],
        "category": preset["category"],
        "tags": preset["tags"],
        "presetPath": c["targetPresetPath"],
        "samplePath": c["targetSamplePath"],
        "createdAt": preset["dateCreated"],
        "modifiedAt": preset["dateModified"],
        "favorite": False,
        "userEdited": False,
        "needsReview": c["needsReview"],
    }
    save_index(layout["index"], upsert_index(index, entry))

    return f"OK   {src.name}  ->  {c['targetSamplePath']}"

def main() -> int:
    ap = argparse.ArgumentParser(description="Import samples into DIDITAGAIN STUDIO as hybrid presets.")
    ap.add_argument("paths", nargs="*")
    ap.add_argument("--inbox", action="store_true",
                    help="Process Documents/DIDITAGAIN STUDIO/Inbox")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--review-json", type=Path, default=None,
                    help="Write candidate list to JSON and exit (no copies)")
    ap.add_argument("--from-review", type=Path, default=None,
                    help="Finalize using an edited candidate list JSON")
    ap.add_argument("--root", type=Path, default=None)
    args = ap.parse_args()

    root = args.root or studio_root()
    layout = ensure_layout(root)
    print(f"Studio root: {root}")

    # If --from-review: load JSON, treat each entry as a finalized candidate.
    if args.from_review:
        candidates = json.loads(args.from_review.read_text(encoding="utf-8"))
    else:
        src_paths = [Path(p) for p in args.paths]
        if args.inbox: src_paths.append(layout["inbox"])
        files = expand(src_paths)
        if not files:
            ap.print_help(); return 1
        candidates = [make_candidate(f, layout) for f in files]

    # Always load the existing index up front so we can hand out unique,
    # human-friendly preset names (e.g. "Guitar 1", "Guitar 2") per category.
    existing_index = load_index(layout["index"])
    assign_auto_names(candidates, existing_index)

    if args.review_json:
        args.review_json.parent.mkdir(parents=True, exist_ok=True)
        args.review_json.write_text(json.dumps(candidates, indent=2), encoding="utf-8")
        print(f"Wrote review JSON: {args.review_json}  ({len(candidates)} files)")
        return 0

    summary = {"imported":0, "skipped":0, "needsReview":0, "byCategory":{}, "errors":[]}
    for c in candidates:
        try:
            msg = finalize(c, layout, force=args.force, dry=args.dry_run)
            print(msg)
            if not args.dry_run:
                summary["imported"] += 1
                summary["byCategory"][c["detectedCategory"]] = \
                    summary["byCategory"].get(c["detectedCategory"], 0) + 1
                if c["needsReview"]: summary["needsReview"] += 1
        except Exception as e:
            summary["errors"].append({"file": c.get("originalFileName",""), "message": str(e)})
            summary["skipped"] += 1
            print(f"ERR  {c.get('originalFileName','?')}: {e}")

    print("---- summary ----")
    print(json.dumps(summary, indent=2))
    return 0

if __name__ == "__main__":
    sys.exit(main())
