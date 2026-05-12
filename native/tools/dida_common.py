"""Shared helpers for the DIDITAGAIN STUDIO importer + migration tools."""
from __future__ import annotations
import json, re, uuid, hashlib
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Filesystem layout
# ---------------------------------------------------------------------------
def studio_root() -> Path:
    """Return the user's `Documents/DIDITAGAIN STUDIO/` folder."""
    home = Path.home()
    candidates = [
        home / "Documents" / "DIDITAGAIN STUDIO",
        home / "OneDrive" / "Documents" / "DIDITAGAIN STUDIO",
    ]
    for c in candidates:
        if c.parent.exists():
            return c
    return candidates[0]

def ensure_layout(root: Path) -> dict[str, Path]:
    """Create the v2 directory layout under `root` and return key paths."""
    paths = {
        "root":           root,
        "inbox":          root / "Inbox",
        "samples":        root / "Samples",
        "samples_imp":    root / "Samples" / "Imported",
        "samples_fac":    root / "Samples" / "Factory",
        "samples_user":   root / "Samples" / "User",
        "presets":        root / "Presets",
        "presets_fac":    root / "Presets" / "Factory",
        "presets_user":   root / "Presets" / "User",
        "metadata":       root / "Metadata" / "Imports",
        "logs":           root / "Logs",
        "settings":       root / "Settings",
        "index":          root / "Presets" / "index.json",
    }
    for k, p in paths.items():
        if k == "index":
            continue
        p.mkdir(parents=True, exist_ok=True)
    return paths

# ---------------------------------------------------------------------------
# Categories — producer-facing, with keyword classifier + legacy mapping.
# ---------------------------------------------------------------------------
NEW_CATEGORIES = [
    "DrillBells","AlienLeads","PainPianos","ChoirsVox","Guitars",
    "DarkPads","Plucks","Bass808","FXRisers","Textures","Uncategorized",
]

# Producer-friendly singular display name per category (used for auto numbering
# like "Guitar 1", "Pad 2", "Choir 3", etc.).
CATEGORY_DISPLAY_NAME = {
    "DrillBells":  "Bell",
    "AlienLeads":  "Lead",
    "PainPianos":  "Piano",
    "ChoirsVox":   "Choir",
    "Guitars":     "Guitar",
    "DarkPads":    "Pad",
    "Plucks":      "Pluck",
    "Bass808":     "808",
    "FXRisers":    "FX",
    "Textures":    "Texture",
    "Uncategorized": "Sound",
}

def display_name_for(category: str) -> str:
    return CATEGORY_DISPLAY_NAME.get(category, "Sound")

_NUM_SUFFIX_RE = re.compile(r"\s+(\d+)\s*$")

def next_preset_number(existing_index: list[dict], category: str,
                       reserved: set[int] | None = None) -> int:
    """Return the next free integer N such that "<DisplayName> N" is unique
    within the given category. Considers both presets already in index.json
    and any numbers reserved during the current batch."""
    base = display_name_for(category).lower()
    used: set[int] = set(reserved or set())
    for e in existing_index:
        if (e.get("category") or "") != category:
            continue
        nm = (e.get("name") or "").strip()
        if not nm.lower().startswith(base.lower()):
            continue
        m = _NUM_SUFFIX_RE.search(nm)
        if m:
            try: used.add(int(m.group(1)))
            except ValueError: pass
    n = 1
    while n in used:
        n += 1
    return n

# Keyword -> category. Order matters: more specific first.
CATEGORY_KEYWORDS: list[tuple[str, str]] = [
    # DrillBells
    *[(k, "DrillBells") for k in
        ("drillbell","darkbell","music_box","music-box","musicbox","glock","kalimba",
         "vibraphone","vibes","celeste","chime","mallet","toy","icy","crystal","bells","bell")],
    # AlienLeads
    *[(k, "AlienLeads") for k in
        ("glogang","alien","laser","futuristic","chief","glide","arp","mono","square","lead")],
    # PainPianos
    *[(k, "PainPianos") for k in
        ("sadpiano","darkkeys","emotional","pain","grand","upright","rhodes","wurli",
         "piano","keys","ep_")],
    # ChoirsVox
    *[(k, "ChoirsVox") for k in
        ("demonvox","gregorian","chant","angel","choir","voices","voice","vocal","vox","ahh","ooh","hum")],
    # Guitars
    *[(k, "Guitars") for k in
        ("pluckedguitar","nylon","electric_guitar","acoustic_guitar","strum","picked","guitar")],
    # DarkPads
    *[(k, "DarkPads") for k in
        ("darkpad","texturepad","atmosphere","ambient","cloud","heaven","drone","airy","lush","pad")],
    # Plucks
    *[(k, "Plucks") for k in ("plucky","pizzicato","pizz","stab","short","harp","pluck")],
    # Bass808
    *[(k, "Bass808") for k in ("distorted808","glidebass","reese","808","sub","lowend","bass")],
    # FXRisers
    *[(k, "FXRisers") for k in
        ("riser","downer","sweep","reverse","crash","boom","fall","transition","impact","hit")],
    # Textures
    *[(k, "Textures") for k in
        ("texture","vinyl","rain","ambience","roomtone","horror","layer","noise","atmosphere")],
]

# Map old/legacy folder names to new categories.
LEGACY_CATEGORY_MAP = {
    "glockenspiel":"DrillBells","musicbox":"DrillBells","vibraphone":"DrillBells",
    "xylophone":"DrillBells","kalimba":"DrillBells","marimba":"DrillBells",
    "piano":"PainPianos","grandpiano":"PainPianos","rhodesep":"PainPianos","wurliep":"PainPianos",
    "clavinet":"PainPianos","harpsichord":"PainPianos","organ":"PainPianos",
    "choir":"ChoirsVox","vocalahh":"ChoirsVox","vocalooh":"ChoirsVox",
    "acousticguitar":"Guitars","electricguitar":"Guitars","nylonguitar":"Guitars",
    "synthlead":"AlienLeads","synthpad":"DarkPads","synthpluck":"Plucks",
    "808kit":"Bass808","bass":"Bass808","synthbass":"Bass808","uprightbass":"Bass808",
    "fx":"FXRisers","misc":"Uncategorized",
    # Brass-family was lead-ish in old taxonomy:
    "brass":"AlienLeads","trumpet":"AlienLeads","trombone":"AlienLeads",
    "frenchhorn":"AlienLeads","saxophone":"AlienLeads",
    # Strings/woodwinds: closest hybrid bucket.
    "strings":"DarkPads","violin":"DarkPads","cello":"DarkPads","harp":"Plucks",
    "pizzicato":"Plucks","flute":"AlienLeads","clarinet":"AlienLeads","oboe":"AlienLeads",
    "panflute":"AlienLeads","sitar":"Plucks",
    "drumkit":"FXRisers","percussion":"FXRisers",
}

def classify_category(stem: str, parent_dir: str | None = None) -> tuple[str, float, str]:
    """Return (category, confidence 0..1, source)."""
    low = stem.lower().replace(" ", "_")
    for kw, cat in CATEGORY_KEYWORDS:
        if kw in low:
            return cat, 0.85, f"keyword:{kw}"
    if parent_dir:
        legacy = parent_dir.lower().replace(" ", "")
        if legacy in LEGACY_CATEGORY_MAP:
            return LEGACY_CATEGORY_MAP[legacy], 0.7, f"legacy:{parent_dir}"
    return "Uncategorized", 0.1, "fallback"

# ---------------------------------------------------------------------------
# Note + velocity parsing
# ---------------------------------------------------------------------------
NOTE_RE = re.compile(
    r"(?<![A-Za-z])([A-Ga-g])([#b]?)(-?\d)?(?![A-Za-z])"
)
LETTER_TO_SEMI = {"C":0,"D":2,"E":4,"F":5,"G":7,"A":9,"B":11}

VEL_TAGS = [
    (r"\bppp\b", 15), (r"\bpp\b", 35), (r"\bp\b", 55),
    (r"\bmp\b", 70), (r"\bmf\b", 90),
    (r"\bfff\b", 127), (r"\bff\b", 120), (r"\bf\b", 105),
    (r"\bsoft\b", 55), (r"\bquiet\b", 45), (r"\bgentle\b", 55), (r"\bmellow\b", 60),
    (r"\bmedium\b", 90), (r"\bmed\b", 90), (r"\bnormal\b", 95),
    (r"\bhard\b", 120), (r"\bloud\b", 120), (r"\bstrong\b", 115),
    (r"\bheavy\b", 120), (r"\bbig\b", 115), (r"\bpunch", 118), (r"\bbright\b", 110),
    (r"\bsustain", 95), (r"\bsistain", 95),
    (r"\blong\s*held\b", 90), (r"\blong[-_\s]?hold\b", 90),
    (r"\blong\b", 90), (r"\bheld\b", 90),
    (r"\bshort\b", 110), (r"\bquick\b", 110), (r"\btight\b", 110),
    (r"\bstab\b", 120), (r"\bstabs\b", 120), (r"\bhit\b", 122), (r"\bhits\b", 122),
    (r"\bstacc", 115), (r"\bspicc", 118), (r"\blegato\b", 95), (r"\bsforz", 125),
    (r"\bmarcato\b", 118), (r"\btenuto\b", 95),
    (r"\baccent", 118), (r"\battack", 118),
    (r"\bswell", 80), (r"\bcrescendo\b", 100),
    (r"\bfade\b", 60), (r"\bdecay\b", 70),
    (r"\bbow", 95), (r"\bpluck", 110),
    (r"\blow\b", 80), (r"\bmid\b", 95), (r"\bhigh\b", 105),
]
VEL_NUM_RE = re.compile(r"(?:^|[_\-\s])v(\d{1,3})\b", re.IGNORECASE)

def detect_note(stem: str) -> Optional[tuple[str, int]]:
    """Return (note_token, midi) if a real note appears in the filename."""
    best = None
    for m in NOTE_RE.finditer(stem):
        letter = m.group(1).upper()
        accidental = m.group(2) or ""
        octave_str = m.group(3)
        semis = LETTER_TO_SEMI[letter]
        if accidental == "#": semis += 1
        elif accidental == "b": semis -= 1
        if octave_str is not None:
            octave = int(octave_str)
            return (f"{letter}{accidental}{octave}", (octave + 1) * 12 + semis)
        if best is None:
            best = (f"{letter}{accidental}", semis)  # no octave yet
    return best

def detect_velocity(stem: str) -> Optional[int]:
    m = VEL_NUM_RE.search(stem)
    if m: return max(1, min(127, int(m.group(1))))
    low = stem.lower()
    for pat, val in VEL_TAGS:
        if re.search(pat, low): return val
    return None

# ---------------------------------------------------------------------------
# Default root note per category (used when filename has no note).
# ---------------------------------------------------------------------------
def default_root_for(category: str) -> tuple[str, int, bool]:
    """Return (note_token, midi, pitchTracking). pitchTracking false for
    inherently unpitched material (FX / textures)."""
    if category == "Bass808":   return ("C2", 36, True)
    if category == "FXRisers":  return ("C4", 60, False)
    if category == "Textures":  return ("C4", 60, False)
    return ("C5", 72, True)

# ---------------------------------------------------------------------------
# Hybrid preset JSON construction (mirrors packages/preset-schema)
# ---------------------------------------------------------------------------
SCHEMA_VERSION = "2.0.0"

def _env(a, d, s, r): return {"attack": a, "decay": d, "sustain": s, "release": r}

def _layer_sample(source, root_note, root_midi, *, pitch_track=True, one_shot=False, loop=False,
                  amp=None, filt=None, volume=0.82):
    return {
        "id":"layer_1","name":"Main Sample","type":"sample","enabled":True,
        "source":source,"rootNote":root_note,"rootMidi":root_midi,
        "pitchTracking":pitch_track,"oneShotMode":one_shot,
        "volume":volume,"pan":0,"pitch":0,"fineTune":0,
        "startOffset":0,"reverse":False,"loop":loop,
        "ampEnvelope": amp or _env(0.001,1.2,0.15,1.8),
        "filter": filt or {"enabled":True,"type":"lowpass","cutoff":8500,"resonance":0.12,"drive":0.05},
    }

def _layer_osc(idx, name, *, waveform="sine", pitch=0, fine=0, volume=0.0, enabled=False, amp=None):
    return {
        "id":f"layer_{idx}","name":name,"type":"oscillator","enabled":enabled,
        "waveform":waveform,"pitch":pitch,"fineTune":fine,
        "volume":volume,"pan":0,
        "ampEnvelope": amp or _env(0.001,0.85,0.05,1.1),
    }

def _layer_noise(idx, name, *, enabled=False, volume=0.04, amp=None):
    return {
        "id":f"layer_{idx}","name":name,"type":"noise","enabled":enabled,
        "noiseType":"white","volume":volume,"pan":0,
        "ampEnvelope": amp or _env(0.001,0.05,0,0.02),
    }

def _gf(cutoff=9000, type_="lowpass"):
    return {"enabled":True,"type":type_,"cutoff":cutoff,"resonance":0.15,"drive":0.05}

def _fx(over=None):
    base = {
        "eq":         {"enabled":True,"lowCut":80,"body":0,"presence":0,"air":0},
        "saturation": {"enabled":False,"mode":"tape","drive":0.1,"mix":0.25},
        "chorus":     {"enabled":False,"rate":0.3,"depth":0.2,"mix":0.2},
        "delay":      {"enabled":False,"sync":True,"time":"1/4","feedback":0.25,"mix":0.15},
        "reverb":     {"enabled":True,"size":0.5,"decay":2.0,"mix":0.2},
        "width":      {"enabled":True,"amount":0.3},
        "limiter":    {"enabled":True,"ceiling":-0.5},
    }
    if over:
        for k, v in over.items(): base[k] = {**base[k], **v}
    return base

def _macros(items):
    return [{"id":f"macro_{i+1}","name":n,"value":v,"targets":t}
            for i, (n, v, t) in enumerate(items)]

def category_template(category: str) -> dict:
    """Return template dict {layers, globalFilter, effects, macros}."""
    if category == "DrillBells":
        return dict(
            layers=[
                _layer_sample("", "C5", 72),
                _layer_osc(2, "Sine Body", waveform="sine", pitch=-12, volume=0.22, enabled=True,
                           amp=_env(0.001,0.85,0.05,1.1)),
                _layer_noise(3, "Air Texture"),
                _layer_osc(4, "Shimmer", waveform="triangle", pitch=12, fine=7, volume=0.08,
                           amp=_env(0.02,1.5,0.12,2.0)),
            ],
            globalFilter=_gf(9000),
            effects=_fx({
                "saturation":{"enabled":True,"drive":0.12,"mix":0.35},
                "chorus":    {"enabled":True,"rate":0.35,"depth":0.18,"mix":0.22},
                "delay":     {"enabled":True,"feedback":0.24,"mix":0.12},
                "reverb":    {"enabled":True,"size":0.72,"decay":2.8,"mix":0.28},
            }),
            macros=_macros([
                ("Darkness",0.5,[{"path":"globalFilter.cutoff","min":3500,"max":12000}]),
                ("Space",   0.5,[{"path":"effects.reverb.mix","min":0,"max":0.55},
                                 {"path":"effects.delay.mix","min":0,"max":0.35}]),
                ("Grit",    0.25,[{"path":"effects.saturation.drive","min":0,"max":0.45}]),
                ("Width",   0.5,[{"path":"effects.width.amount","min":0,"max":0.75},
                                 {"path":"effects.chorus.mix","min":0,"max":0.45}]),
            ]),
        )
    if category == "Bass808":
        return dict(
            layers=[
                _layer_sample("", "C2", 36, amp=_env(0.001,1.0,0.85,0.8),
                              filt={"enabled":True,"type":"lowpass","cutoff":4500,"resonance":0.1,"drive":0.1}),
                _layer_osc(2, "Sine Sub", waveform="sine", volume=0.3, enabled=True,
                           amp=_env(0.001,1.0,0.85,0.8)),
                _layer_noise(3, "Grit"),
                _layer_osc(4, "Distort Layer", waveform="saw"),
            ],
            globalFilter=_gf(5000),
            effects=_fx({
                "saturation":{"enabled":True,"mode":"diode","drive":0.3,"mix":0.4},
                "chorus":{"enabled":False},"reverb":{"enabled":False},"width":{"enabled":False},
            }),
            macros=_macros([
                ("Drive",0.3,[{"path":"effects.saturation.drive","min":0,"max":0.7}]),
                ("Glide",0.0,[{"path":"globalFilter.cutoff","min":2500,"max":8000}]),
                ("Tone", 0.5,[{"path":"globalFilter.cutoff","min":2500,"max":9000}]),
                ("Punch",0.5,[{"path":"layers.0.ampEnvelope.decay","min":0.3,"max":2.0}]),
            ]),
        )
    if category == "FXRisers":
        return dict(
            layers=[
                _layer_sample("", "C4", 60, pitch_track=False, one_shot=True,
                              amp=_env(0.001,4.0,0.0,3.0)),
                _layer_osc(2, "Noise Wash"),
                _layer_noise(3, "Air"),
                _layer_osc(4, "Sweep", waveform="saw"),
            ],
            globalFilter=_gf(14000, "highpass"),
            effects=_fx({"reverb":{"enabled":True,"size":0.95,"decay":6.0,"mix":0.5},
                         "width":{"enabled":True,"amount":0.7}}),
            macros=_macros([
                ("Size",   0.6,[{"path":"effects.reverb.size","min":0.3,"max":0.99}]),
                ("Reverse",0.0,[{"path":"layers.0.startOffset","min":0,"max":1}]),
                ("Space",  0.6,[{"path":"effects.reverb.mix","min":0,"max":0.55}]),
                ("Tone",   0.5,[{"path":"globalFilter.cutoff","min":800,"max":16000}]),
            ]),
        )
    if category == "Textures":
        return dict(
            layers=[
                _layer_sample("", "C4", 60, pitch_track=False, loop=True,
                              amp=_env(0.6,1.0,0.85,2.5)),
                _layer_osc(2, "Drone", waveform="sine"),
                _layer_noise(3, "Noise", enabled=True, volume=0.04,
                             amp=_env(0.5,2.0,0.4,2.0)),
                _layer_osc(4, "Air", pitch=12),
            ],
            globalFilter=_gf(9000),
            effects=_fx({"reverb":{"enabled":True,"size":0.85,"decay":4.0,"mix":0.4},
                         "width":{"enabled":True,"amount":0.6}}),
            macros=_macros([
                ("Texture",0.5,[{"path":"layers.2.volume","min":0,"max":0.2}]),
                ("Space",  0.5,[{"path":"effects.reverb.mix","min":0,"max":0.55}]),
                ("Width",  0.5,[{"path":"effects.width.amount","min":0,"max":0.75}]),
                ("Tone",   0.5,[{"path":"globalFilter.cutoff","min":3500,"max":12000}]),
            ]),
        )
    if category == "PainPianos":
        return dict(
            layers=[
                _layer_sample("", "C4", 60, amp=_env(0.002,1.5,0.6,2.4),
                              filt={"enabled":True,"type":"lowpass","cutoff":7500,"resonance":0.08,"drive":0}),
                _layer_osc(2, "Pad Under", waveform="triangle", pitch=-12, volume=0.1),
                _layer_noise(3, "Air"),
                _layer_osc(4, "Shimmer", pitch=12),
            ],
            globalFilter=_gf(7800),
            effects=_fx({"saturation":{"enabled":True,"drive":0.08,"mix":0.25},
                         "reverb":{"enabled":True,"size":0.8,"decay":3.5,"mix":0.32}}),
            macros=_macros([
                ("Softness",0.5,[{"path":"layers.0.filter.cutoff","min":3500,"max":9000}]),
                ("Room",    0.4,[{"path":"effects.reverb.mix","min":0,"max":0.55}]),
                ("Dark",    0.5,[{"path":"globalFilter.cutoff","min":3500,"max":12000}]),
                ("Width",   0.4,[{"path":"effects.width.amount","min":0,"max":0.75}]),
            ]),
        )
    if category == "ChoirsVox":
        return dict(
            layers=[
                _layer_sample("", "C4", 60, amp=_env(0.4,1.0,0.85,2.5)),
                _layer_osc(2, "Shimmer", waveform="triangle", pitch=12, volume=0.06),
                _layer_noise(3, "Air"),
                _layer_osc(4, "Sub", pitch=-12),
            ],
            globalFilter=_gf(8500),
            effects=_fx({"chorus":{"enabled":True,"rate":0.25,"depth":0.2,"mix":0.25},
                         "reverb":{"enabled":True,"size":0.85,"decay":4.0,"mix":0.4},
                         "width":{"enabled":True,"amount":0.6}}),
            macros=_macros([
                ("Air",     0.5,[{"path":"effects.eq.air","min":0,"max":0.4}]),
                ("Space",   0.5,[{"path":"effects.reverb.mix","min":0,"max":0.55}]),
                ("Width",   0.5,[{"path":"effects.width.amount","min":0,"max":0.75}]),
                ("Darkness",0.4,[{"path":"globalFilter.cutoff","min":3500,"max":12000}]),
            ]),
        )
    if category == "Guitars":
        return dict(
            layers=[
                _layer_sample("", "C4", 60, amp=_env(0.002,1.0,0.4,1.2)),
                _layer_osc(2, "Body", pitch=-12),
                _layer_noise(3, "Air"),
                _layer_osc(4, "Air"),
            ],
            globalFilter=_gf(10000),
            effects=_fx({"eq":{"enabled":True,"lowCut":120,"body":0.05,"presence":0.1,"air":0.12},
                         "reverb":{"enabled":True,"size":0.55,"decay":2.0,"mix":0.2}}),
            macros=_macros([
                ("Tone",    0.5,[{"path":"globalFilter.cutoff","min":3000,"max":14000}]),
                ("Room",    0.4,[{"path":"effects.reverb.mix","min":0,"max":0.5}]),
                ("Width",   0.4,[{"path":"effects.width.amount","min":0,"max":0.75}]),
                ("Softness",0.4,[{"path":"globalFilter.cutoff","min":3500,"max":12000}]),
            ]),
        )
    if category == "DarkPads":
        return dict(
            layers=[
                _layer_sample("", "C4", 60, amp=_env(0.6,2.0,0.85,3.5)),
                _layer_osc(2, "Sub", waveform="sine", pitch=-12, volume=0.18, enabled=True,
                           amp=_env(0.6,2.0,0.85,3.5)),
                _layer_noise(3, "Air", enabled=True, volume=0.03,
                             amp=_env(0.5,2.0,0.5,2.0)),
                _layer_osc(4, "Shimmer", waveform="triangle", pitch=12, volume=0.1),
            ],
            globalFilter=_gf(6500),
            effects=_fx({"chorus":{"enabled":True,"rate":0.18,"depth":0.3,"mix":0.35},
                         "reverb":{"enabled":True,"size":0.9,"decay":5.0,"mix":0.45},
                         "width":{"enabled":True,"amount":0.65}}),
            macros=_macros([
                ("Motion",  0.4,[{"path":"effects.chorus.depth","min":0,"max":0.6}]),
                ("Air",     0.5,[{"path":"effects.eq.air","min":0,"max":0.4}]),
                ("Space",   0.6,[{"path":"effects.reverb.mix","min":0,"max":0.55}]),
                ("Darkness",0.5,[{"path":"globalFilter.cutoff","min":3500,"max":12000}]),
            ]),
        )
    if category == "Plucks":
        return dict(
            layers=[
                _layer_sample("", "C5", 72, amp=_env(0.001,0.35,0.05,0.6)),
                _layer_osc(2, "Sine", waveform="sine", pitch=-12, volume=0.15),
                _layer_noise(3, "Air"),
                _layer_osc(4, "Air", pitch=12, volume=0.05),
            ],
            globalFilter=_gf(11000),
            effects=_fx({"delay":{"enabled":True,"time":"1/8","feedback":0.28,"mix":0.18},
                         "reverb":{"enabled":True,"size":0.5,"decay":1.8,"mix":0.2}}),
            macros=_macros([
                ("Snap",      0.4,[{"path":"layers.0.ampEnvelope.attack","min":0.001,"max":0.05}]),
                ("Space",     0.5,[{"path":"effects.reverb.mix","min":0,"max":0.55}]),
                ("Brightness",0.6,[{"path":"globalFilter.cutoff","min":4000,"max":16000}]),
                ("Width",     0.4,[{"path":"effects.width.amount","min":0,"max":0.75}]),
            ]),
        )
    if category == "AlienLeads":
        return dict(
            layers=[
                _layer_sample("", "C4", 60, amp=_env(0.005,0.4,0.7,0.6)),
                _layer_osc(2, "Square Support", waveform="square", enabled=True, volume=0.25,
                           amp=_env(0.005,0.4,0.7,0.6)),
                _layer_noise(3, "Air"),
                _layer_osc(4, "Saw Detune", waveform="saw", fine=9, volume=0.18),
            ],
            globalFilter=_gf(11000),
            effects=_fx({"chorus":{"enabled":True,"rate":0.4,"depth":0.25,"mix":0.3},
                         "delay":{"enabled":True,"time":"1/8","feedback":0.3,"mix":0.18},
                         "reverb":{"enabled":True,"size":0.6,"decay":2.2,"mix":0.22}}),
            macros=_macros([
                ("Glide",0.2,[{"path":"globalFilter.cutoff","min":8000,"max":14000}]),
                ("Bite", 0.3,[{"path":"effects.saturation.drive","min":0,"max":0.45}]),
                ("Space",0.5,[{"path":"effects.reverb.mix","min":0,"max":0.55}]),
                ("Width",0.5,[{"path":"effects.width.amount","min":0,"max":0.75}]),
            ]),
        )
    # Uncategorized
    return dict(
        layers=[
            _layer_sample("", "C5", 72),
            _layer_osc(2, "Body"),
            _layer_noise(3, "Air"),
            _layer_osc(4, "Aux"),
        ],
        globalFilter=_gf(10000),
        effects=_fx({"reverb":{"enabled":True,"size":0.5,"decay":2.0,"mix":0.18}}),
        macros=_macros([
            ("Darkness",0.5,[{"path":"globalFilter.cutoff","min":3500,"max":12000}]),
            ("Space",   0.4,[{"path":"effects.reverb.mix","min":0,"max":0.55}]),
            ("Grit",    0.2,[{"path":"effects.saturation.drive","min":0,"max":0.45}]),
            ("Width",   0.4,[{"path":"effects.width.amount","min":0,"max":0.75}]),
        ]),
    )

def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()

def build_preset(*, name, category, sample_path_rel, metadata_path_rel,
                 original_filename, root_note, root_midi, root_note_source,
                 pitch_tracking, one_shot=False, needs_review=False, tags=None,
                 bank="User", author="User") -> dict:
    t = category_template(category)
    layer1 = t["layers"][0]
    layer1["source"] = sample_path_rel
    layer1["rootNote"] = root_note
    layer1["rootMidi"] = root_midi
    layer1["pitchTracking"] = pitch_tracking
    if one_shot: layer1["oneShotMode"] = True

    return {
        "schemaVersion": SCHEMA_VERSION,
        "plugin": "DIDITAGAIN STUDIO",
        "presetId": str(uuid.uuid4()),
        "name": name,
        "bank": bank,
        "category": category,
        "subCategory": "Imported One-Shot",
        "author": author,
        "dateCreated": now_iso(),
        "dateModified": now_iso(),
        "genre": [],
        "mood": [],
        "tags": tags or [category.lower(), "imported", "hybrid"],
        "engine": "hybrid",
        "sourceImport": {
            "originalFileName": original_filename,
            "samplePath": sample_path_rel,
            "metadataPath": metadata_path_rel,
            "detectedRootNote": root_note,
            "rootMidi": root_midi,
            "rootNoteSource": root_note_source,
            "confidence": 0.95 if root_note_source == "filename" else 0.4,
            "pitchTracking": pitch_tracking,
        },
        "quality": {
            "gainNormalized": False,
            "rootNoteVerified": root_note_source in ("filename","manual"),
            "loopChecked": False,
            "needsReview": needs_review,
            "volumeBalanced": False,
        },
        "layers": t["layers"],
        "globalFilter": t["globalFilter"],
        "effects": t["effects"],
        "macros": t["macros"],
    }

# ---------------------------------------------------------------------------
# Index file (Presets/index.json)
# ---------------------------------------------------------------------------
def load_index(path: Path) -> list[dict]:
    if not path.exists(): return []
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return []

def save_index(path: Path, entries: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(entries, indent=2), encoding="utf-8")

def upsert_index(entries: list[dict], entry: dict) -> list[dict]:
    """Replace by presetPath; otherwise append."""
    out = [e for e in entries if e.get("presetPath") != entry.get("presetPath")]
    out.append(entry)
    return out
