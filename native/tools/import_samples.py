#!/usr/bin/env python3
"""
import_samples.py — Auto-route one-shots into the DIDITAGAIN STUDIO sample tree.

Usage:
    python import_samples.py <file_or_folder> [<file_or_folder> ...]
    python import_samples.py --inbox            # process Documents/DIDITAGAIN STUDIO/Inbox
    python import_samples.py --dry-run <files>  # show what would happen, no copy

What it does
------------
1. Detects the **musical note** mentioned in the filename
   (C, C#, Db, D, D#, ... B, with optional octave like C3 / D#4 / A-1).
   If no octave is given, a sensible default is chosen per instrument
   category (e.g. Brass -> 3, Bass -> 2, Bell -> 5).

2. Detects the **instrument category / sub-folder** by keyword matching
   against the filename (case-insensitive). Matches the folder names that
   ship as factory presets (Brass, Trumpet, Piano, RhodesEP, 808Kit, ...).

3. Detects an optional **velocity layer** from tags like "soft / mf / ff /
   v90" and maps it to a 1-127 upper bound.

4. Copies the file into:
       <UserDocuments>/DIDITAGAIN STUDIO/Samples/<Category>/
   renamed to the engine's convention:
       <Category>_<Note><Octave>[_v<Vel>].<ext>

Run it any time you drop new samples in. Existing files are not overwritten
unless --force is passed.
"""

from __future__ import annotations
import argparse, os, re, shutil, sys
from pathlib import Path

# ---------------------------------------------------------------------------
# 1. Where the plugin reads samples from
# ---------------------------------------------------------------------------
def samples_root() -> Path:
    home = Path.home()
    # Windows: Documents may live under OneDrive; check both.
    candidates = [
        home / "Documents" / "DIDITAGAIN STUDIO" / "Samples",
        home / "OneDrive" / "Documents" / "DIDITAGAIN STUDIO" / "Samples",
    ]
    for c in candidates:
        if c.parent.exists():
            return c
    return candidates[0]

# ---------------------------------------------------------------------------
# 2. Categories — must match the factory preset folder names
# ---------------------------------------------------------------------------
# Keyword -> (folder, default_octave)
# Order matters: more specific keywords first.
CATEGORY_RULES: list[tuple[str, str, int]] = [
    # keyword,            folder,            default octave
    ("trumpet",           "Trumpet",         4),
    ("trombone",          "Trombone",        3),
    ("french horn",       "FrenchHorn",      3),
    ("frenchhorn",        "FrenchHorn",      3),
    ("horn",              "FrenchHorn",      3),
    ("sax",               "Saxophone",       3),
    ("brass",             "Brass",           3),

    ("violin",            "Violin",          4),
    ("cello",             "Cello",           3),
    ("pizz",              "Pizzicato",       3),
    ("harp",              "Harp",            4),
    ("string",            "Strings",         3),

    ("grand piano",       "GrandPiano",      4),
    ("grandpiano",        "GrandPiano",      4),
    ("rhodes",            "RhodesEP",        4),
    ("wurli",             "WurliEP",         4),
    ("ep ",               "RhodesEP",        4),
    ("electric piano",    "RhodesEP",        4),
    ("clav",              "Clavinet",        4),
    ("harpsichord",       "Harpsichord",     4),
    ("organ",             "Organ",           3),
    ("piano",             "Piano",           4),

    ("marimba",           "Marimba",         4),
    ("vibraphone",        "Vibraphone",      4),
    ("vibes",             "Vibraphone",      4),
    ("xylophone",         "Xylophone",       5),
    ("glock",             "Glockenspiel",    6),
    ("music box",         "MusicBox",        5),
    ("musicbox",          "MusicBox",        5),
    ("kalimba",           "Kalimba",         5),
    ("bell",              "Glockenspiel",    5),

    ("nylon",             "NylonGuitar",     3),
    ("acoustic guitar",   "AcousticGuitar",  3),
    ("electric guitar",   "ElectricGuitar",  3),
    ("guitar",            "AcousticGuitar",  3),

    ("upright bass",      "UprightBass",     2),
    ("synth bass",        "SynthBass",       2),
    ("808",               "808Kit",          2),
    ("bass",              "Bass",            2),

    ("flute",             "Flute",           5),
    ("clarinet",          "Clarinet",        4),
    ("oboe",              "Oboe",            4),
    ("pan flute",         "PanFlute",        5),
    ("panflute",          "PanFlute",        5),

    ("choir",             "Choir",           4),
    ("vocal ahh",         "VocalAhh",        4),
    ("vocal ooh",         "VocalOoh",        4),
    ("ahh",               "VocalAhh",        4),
    ("ooh",               "VocalOoh",        4),
    ("vocal",             "Choir",           4),

    ("kit",               "DrumKit",         3),
    ("drum",              "DrumKit",         3),
    ("perc",              "Percussion",      3),

    ("lead",              "SynthLead",       4),
    ("pad",               "SynthPad",        3),
    ("pluck",             "SynthPluck",      4),
    ("fx",                "FX",              4),
    ("synth",             "SynthLead",       4),

    ("sitar",             "Sitar",           3),
]

# ---------------------------------------------------------------------------
# 3. Note + velocity parsing
# ---------------------------------------------------------------------------
NOTE_RE = re.compile(
    r"(?<![A-Za-z])"            # not preceded by a letter
    r"([A-Ga-g])"               # note letter
    r"([#b]?)"                  # accidental
    r"(-?\d)?"                  # optional octave  (−1 .. 9)
    r"(?![A-Za-z])"             # not followed by a letter
)

LETTER_TO_SEMI = {"C":0,"D":2,"E":4,"F":5,"G":7,"A":9,"B":11}

VEL_TAGS = [
    # Classical dynamics
    (r"\bppp\b", 15), (r"\bpp\b", 35), (r"\bp\b", 55),
    (r"\bmp\b", 70), (r"\bmf\b", 90),
    (r"\bfff\b", 127), (r"\bff\b", 120), (r"\bf\b", 105),

    # Loudness words
    (r"\bsoft\b", 55), (r"\bquiet\b", 45), (r"\bgentle\b", 55), (r"\bmellow\b", 60),
    (r"\bmedium\b", 90), (r"\bmed\b", 90), (r"\bnormal\b", 95),
    (r"\bhard\b", 120), (r"\bloud\b", 120), (r"\bstrong\b", 115),
    (r"\bheavy\b", 120), (r"\bbig\b", 115), (r"\bpunch", 118), (r"\bbright\b", 110),

    # Articulation / length words (mapped to a sensible velocity)
    (r"\bsustain", 95),    (r"\bsistain", 95),     # common typo "sistained"
    (r"\blong\s*held\b", 90), (r"\blong[-_\s]?hold\b", 90),
    (r"\blong\b", 90),     (r"\bheld\b", 90),
    (r"\bshort\b", 110),   (r"\bquick\b", 110),    (r"\btight\b", 110),
    (r"\bstab\b", 120),    (r"\bstabs\b", 120),    (r"\bhit\b", 122), (r"\bhits\b", 122),
    (r"\bstacc", 115),     (r"\bspicc", 118),      # staccato / spiccato
    (r"\blegato\b", 95),   (r"\bsforz", 125),      # legato / sforzando
    (r"\bmarcato\b", 118), (r"\btenuto\b", 95),
    (r"\baccent", 118),    (r"\battack", 118),
    (r"\bswell", 80),      (r"\bcrescendo\b", 100),
    (r"\bfade\b", 60),     (r"\bdecay\b", 70),
    (r"\bbow", 95),        (r"\bpluck", 110),

    # Register words (loose hint, not a true velocity)
    (r"\blow\b", 80), (r"\bmid\b", 95), (r"\bhigh\b", 105),
]
VEL_NUM_RE = re.compile(r"(?:^|[_\-\s])v(\d{1,3})\b", re.IGNORECASE)

def detect_note(stem: str, default_octave: int) -> tuple[str, int] | None:
    """Return (note_token_for_filename, midi) or None."""
    # Walk every match, prefer the one with an explicit octave.
    best = None
    for m in NOTE_RE.finditer(stem):
        letter = m.group(1).upper()
        accidental = m.group(2) or ""
        octave_str = m.group(3)
        # Skip lone "B" in "Bb" — the regex would already group accidental.
        # Skip degenerate matches where the "letter" is part of a word.
        # We already required no-letter neighbours via lookarounds.
        semis = LETTER_TO_SEMI[letter]
        if accidental == "#": semis += 1
        elif accidental == "b": semis -= 1
        if octave_str is not None:
            octave = int(octave_str)
            note_token = f"{letter}{accidental}{octave}"
            midi = (octave + 1) * 12 + semis
            return (note_token, midi)
        if best is None:
            note_token = f"{letter}{accidental}{default_octave}"
            midi = (default_octave + 1) * 12 + semis
            best = (note_token, midi)
    return best

def detect_velocity(stem: str) -> int | None:
    m = VEL_NUM_RE.search(stem)
    if m:
        return max(1, min(127, int(m.group(1))))
    low = stem.lower()
    for pat, val in VEL_TAGS:
        if re.search(pat, low):
            return val
    return None

def detect_category(stem: str) -> tuple[str, int]:
    low = stem.lower()
    for kw, folder, oct_ in CATEGORY_RULES:
        if kw in low:
            return folder, oct_
    return "Misc", 4

# ---------------------------------------------------------------------------
# 4. File routing
# ---------------------------------------------------------------------------
AUDIO_EXTS = {".wav", ".flac", ".ogg", ".mp3", ".aif", ".aiff"}

def route_one(src: Path, root: Path, force: bool, dry: bool) -> str:
    if src.suffix.lower() not in AUDIO_EXTS:
        return f"skip  (not audio): {src.name}"

    stem = src.stem
    folder, default_oct = detect_category(stem)
    note = detect_note(stem, default_oct)
    if note is None:
        return f"skip  (no note in name): {src.name}"
    note_token, _midi = note
    vel = detect_velocity(stem)

    new_stem = f"{folder}_{note_token}"
    if vel is not None:
        new_stem += f"_v{vel}"
    dst_dir = root / folder
    dst = dst_dir / (new_stem + src.suffix.lower())

    if dry:
        return f"DRY   {src.name}  ->  {folder}/{dst.name}"

    dst_dir.mkdir(parents=True, exist_ok=True)
    if dst.exists() and not force:
        # Avoid clobber: append a counter.
        i = 2
        while True:
            candidate = dst_dir / f"{new_stem}_{i}{src.suffix.lower()}"
            if not candidate.exists():
                dst = candidate
                break
            i += 1
    shutil.copy2(src, dst)
    return f"copy  {src.name}  ->  {folder}/{dst.name}"

def expand(paths: list[Path]) -> list[Path]:
    out = []
    for p in paths:
        if p.is_dir():
            out.extend(sorted(p.rglob("*")))
        elif p.exists():
            out.append(p)
    return [p for p in out if p.is_file()]

# ---------------------------------------------------------------------------
# 5. CLI
# ---------------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(description="Route one-shots into the DIDITAGAIN STUDIO sample tree.")
    ap.add_argument("paths", nargs="*", help="Files or folders to import")
    ap.add_argument("--inbox", action="store_true",
                    help="Process Documents/DIDITAGAIN STUDIO/Inbox")
    ap.add_argument("--dry-run", action="store_true", help="Show actions without copying")
    ap.add_argument("--force", action="store_true", help="Overwrite existing files")
    ap.add_argument("--root", type=Path, default=None,
                    help="Override the destination Samples root")
    args = ap.parse_args()

    root = args.root or samples_root()
    print(f"Samples root: {root}")

    src_paths = [Path(p) for p in args.paths]
    if args.inbox:
        inbox = root.parent / "Inbox"
        if inbox.is_dir():
            src_paths.append(inbox)
        else:
            print(f"(inbox not found: {inbox})")

    files = expand(src_paths)
    if not files:
        ap.print_help()
        return 1

    for f in files:
        print(route_one(f, root, args.force, args.dry_run))
    return 0

if __name__ == "__main__":
    sys.exit(main())
