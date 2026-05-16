/**
 * Multisample file routing for DIDITAGAIN STUDIO.
 *
 * Parses uploaded WAV filenames like `Guitar_C3.wav` or `Dark_Guitar_F#4.wav`,
 * extracts the instrument/preset name + root note, routes the file to the
 * correct category folder, and builds an auto_multisample manifest with
 * proper zone boundaries based on the standard 16-note grid (C/D#/F#/A
 * across octaves 2..5).
 *
 * Pure functions — no I/O. Safe to call in browser or edge runtime.
 */

// ---------- Note model ----------

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"] as const;
const NOTE_TO_PC: Record<string, number> = {
  C: 0, "C#": 1, D: 2, "D#": 3, E: 4, F: 5, "F#": 6, G: 7, "G#": 8, A: 9, "A#": 10, B: 11,
};

export const midiToName = (midi: number): string => {
  const pc = ((midi % 12) + 12) % 12;
  const octave = Math.floor(midi / 12) - 1;
  return `${NOTE_NAMES[pc]}${octave}`;
};

export const noteToMidi = (note: string): number | null => {
  const m = /^([A-Ga-g])(#|b)?(-?\d+)$/.exec(note.trim());
  if (!m) return null;
  let pc = NOTE_TO_PC[m[1].toUpperCase()];
  if (pc === undefined) return null;
  if (m[2] === "#") pc += 1;
  else if (m[2] === "b") pc -= 1;
  const oct = parseInt(m[3], 10);
  return (oct + 1) * 12 + pc;
};

// ---------- Supported root grid ----------

/** The 16 supported root notes: C, D#, F#, A across octaves 2..5. */
export const SUPPORTED_ROOTS: string[] = (() => {
  const out: string[] = [];
  for (let oct = 2; oct <= 5; oct++) {
    for (const n of ["C", "D#", "F#", "A"]) out.push(`${n}${oct}`);
  }
  return out;
})();

export const SUPPORTED_ROOT_MIDIS: number[] = SUPPORTED_ROOTS
  .map((n) => noteToMidi(n)!)
  .sort((a, b) => a - b);

/**
 * Zone coverage for the 4-per-octave grid.
 *   C  covers [B(prev) , C#]   (root..root+1)  — wait: spec says C2 covers B1–C#2
 *   D# covers [D, E]           (root-1..root+1)
 *   F# covers [F, G]           (root-1..root+1)
 *   A  covers [G#, B]          (root-1..root+1)
 * Per the explicit table, each root covers 3 semitones except C which spans
 * across the octave boundary. Encoded directly below.
 */
const COVERAGE_BELOW: Record<string, number> = { C: 1, "D#": 1, "F#": 1, A: 1 };
const COVERAGE_ABOVE: Record<string, number> = { C: 1, "D#": 1, "F#": 1, A: 2 }; // A covers up to B (root+2)? recheck

// From spec: A2 covers G#2–B2 → A=57, G#=56, B=59 → below 1, above 2.
//            C3 covers C3–C#3 → C=60: below 0, above 1.
// Update tables accordingly.
const SPAN_BELOW: Record<string, number> = { C: 0, "D#": 1, "F#": 1, A: 1 };
const SPAN_ABOVE: Record<string, number> = { C: 1, "D#": 1, "F#": 1, A: 2 };

export interface ZoneBounds { lowMidi: number; highMidi: number; }

export const zoneForRoot = (rootMidi: number): ZoneBounds => {
  const name = midiToName(rootMidi).replace(/-?\d+$/, "");
  const below = SPAN_BELOW[name] ?? 1;
  const above = SPAN_ABOVE[name] ?? 1;
  return { lowMidi: rootMidi - below, highMidi: rootMidi + above };
};

// ---------- Filename parsing ----------

const CATEGORY_RULES: { match: RegExp; category: string }[] = [
  { match: /guitar/i, category: "Guitars" },
  { match: /(choir|vox|vocal|aah|ooh)/i, category: "Choirs" },
  { match: /(piano|keys)/i, category: "Pianos" },
  { match: /(bell|pluck)/i, category: "Bells" },
  { match: /(808|bass)/i, category: "808s" },
  { match: /string/i, category: "Strings" },
  { match: /(brass|horn|trumpet)/i, category: "Brass" },
];

export const routeCategory = (instrumentName: string): string => {
  for (const r of CATEGORY_RULES) if (r.match.test(instrumentName)) return r.category;
  return "Uncategorized";
};

const NOTE_TOKEN_RE =
  /^([A-Ga-g])(#|sharp|s|b|flat)?(-?\d+)$/i;

/** Normalize a raw note token like `Dsharp3`, `Ds3`, `D#3`, `Db3` to `D#3`/`Db3`/`D3`. */
const normalizeNoteToken = (raw: string): string | null => {
  const m = NOTE_TOKEN_RE.exec(raw);
  if (!m) return null;
  const letter = m[1].toUpperCase();
  const accMatch = m[2]?.toLowerCase();
  let acc = "";
  if (accMatch === "#" || accMatch === "sharp" || accMatch === "s") acc = "#";
  else if (accMatch === "b" || accMatch === "flat") acc = "b";
  const oct = parseInt(m[3], 10);
  const candidate = `${letter}${acc}${oct}`;
  if (noteToMidi(candidate) === null) return null;
  return candidate;
};

export interface ParsedFileName {
  ok: boolean;
  originalName: string;
  baseName: string;            // without extension
  instrumentName: string;      // e.g. "Guitar", "Dark Guitar", "Choir Aah"
  rootNote: string | null;     // normalized, e.g. "D#3"
  rootMidi: number | null;
  category: string;            // routed category
  warnings: string[];
  errors: string[];
}

const SAFE_NAME_RE = /[^A-Za-z0-9#_\-]+/g;

/** Convert spaces / dashes / odd chars to underscores; keep #. */
export const sanitizeForFs = (s: string): string =>
  s.replace(/[\s\-]+/g, "_").replace(SAFE_NAME_RE, "_").replace(/_+/g, "_").replace(/^_|_$/g, "");

export const parseSampleFileName = (filename: string): ParsedFileName => {
  const warnings: string[] = [];
  const errors: string[] = [];
  const lower = filename.toLowerCase();
  const baseName = filename.replace(/\.[^.]+$/, "");

  const result: ParsedFileName = {
    ok: false,
    originalName: filename,
    baseName,
    instrumentName: baseName,
    rootNote: null,
    rootMidi: null,
    category: "Uncategorized",
    warnings,
    errors,
  };

  if (!lower.endsWith(".wav")) {
    errors.push("File extension must be .wav");
    return result;
  }

  // Split on _, -, or whitespace. Walk from the right looking for the last token
  // that parses as a note.
  const tokens = baseName.split(/[\s_\-]+/).filter(Boolean);
  if (tokens.length < 2) {
    errors.push("Filename must include an instrument name and a root note (e.g. Guitar_C3.wav)");
    return result;
  }

  let noteIdx = -1;
  let normalizedNote: string | null = null;
  for (let i = tokens.length - 1; i >= 0; i--) {
    const norm = normalizeNoteToken(tokens[i]);
    if (norm) { noteIdx = i; normalizedNote = norm; break; }
  }
  if (noteIdx === -1 || !normalizedNote) {
    errors.push("Could not detect a root note at the end of the filename");
    return result;
  }

  const rootMidi = noteToMidi(normalizedNote)!;
  const instrumentTokens = tokens.slice(0, noteIdx);
  if (instrumentTokens.length === 0) {
    errors.push("Filename is missing an instrument name before the root note");
    return result;
  }

  const instrumentName = instrumentTokens.join(" ").trim();
  const category = routeCategory(instrumentName);

  if (!SUPPORTED_ROOT_MIDIS.includes(rootMidi)) {
    warnings.push(
      `Root ${normalizedNote} is outside the supported grid (C/D#/F#/A across octaves 2–5).`,
    );
  }

  result.ok = errors.length === 0;
  result.rootNote = normalizedNote;
  result.rootMidi = rootMidi;
  result.instrumentName = instrumentName;
  result.category = category;
  return result;
};

// ---------- Routing & manifest ----------

export interface RoutedFile {
  parsed: ParsedFileName;
  /** Path inside the samples root, e.g. `Guitars/Guitar_C3.wav`. */
  targetPath: string;
  /** Group key (category + sanitized instrument), e.g. `Guitars/Guitar`. */
  presetKey: string;
}

export const routeFile = (filename: string): RoutedFile => {
  const parsed = parseSampleFileName(filename);
  const safeInstrument = sanitizeForFs(parsed.instrumentName || "Unknown");
  const safeNote = parsed.rootNote ?? "Unknown";
  const finalName = `${safeInstrument}_${safeNote}.wav`;
  const targetPath = `${parsed.category}/${finalName}`;
  return {
    parsed,
    targetPath,
    presetKey: `${parsed.category}/${safeInstrument}`,
  };
};

export interface ManifestSample {
  file: string;
  rootNote: string;
  lowKey: string;
  highKey: string;
  velocityMin: number;
  velocityMax: number;
  gainDb: number;
  tuningCents: number;
}

export interface MultisamplePresetManifest {
  presetName: string;
  category: string;
  mappingMode: "auto_multisample";
  samples: ManifestSample[];
  warnings: string[];
}

export interface GenerateManifestsResult {
  manifests: MultisamplePresetManifest[];
  globalWarnings: string[];
  globalErrors: string[];
  duplicates: { presetKey: string; rootNote: string; files: string[] }[];
  missingRoots: { presetKey: string; missing: string[] }[];
}

/**
 * Group a list of routed files by preset and build manifests. Detects:
 *   - duplicate root notes within a preset
 *   - missing roots from the supported grid (warning only)
 */
export const generateManifests = (
  files: RoutedFile[],
  opts: { category?: string; presetName?: string } = {},
): GenerateManifestsResult => {
  const globalWarnings: string[] = [];
  const globalErrors: string[] = [];
  const duplicates: GenerateManifestsResult["duplicates"] = [];
  const missingRoots: GenerateManifestsResult["missingRoots"] = [];

  const groups = new Map<string, RoutedFile[]>();
  for (const f of files) {
    if (!f.parsed.ok) {
      globalErrors.push(`${f.parsed.originalName}: ${f.parsed.errors.join(", ")}`);
      continue;
    }
    const key = f.presetKey;
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key)!.push(f);
  }

  const manifests: MultisamplePresetManifest[] = [];

  for (const [presetKey, items] of groups) {
    const sample0 = items[0];
    const category = opts.category ?? sample0.parsed.category;
    const presetName = opts.presetName ?? sample0.parsed.instrumentName;

    // Duplicate detection.
    const byRoot = new Map<string, RoutedFile[]>();
    for (const it of items) {
      const r = it.parsed.rootNote!;
      if (!byRoot.has(r)) byRoot.set(r, []);
      byRoot.get(r)!.push(it);
    }
    for (const [root, group] of byRoot) {
      if (group.length > 1) {
        duplicates.push({
          presetKey,
          rootNote: root,
          files: group.map((g) => g.parsed.originalName),
        });
      }
    }

    // Missing-root warning across the full grid.
    const present = new Set(items.map((i) => i.parsed.rootNote!));
    const missing = SUPPORTED_ROOTS.filter((r) => !present.has(r));
    if (missing.length > 0 && missing.length < SUPPORTED_ROOTS.length) {
      missingRoots.push({ presetKey, missing });
    }

    const samples: ManifestSample[] = items
      .filter((it) => it.parsed.rootMidi !== null)
      .sort((a, b) => a.parsed.rootMidi! - b.parsed.rootMidi!)
      .map((it) => {
        const { lowMidi, highMidi } = zoneForRoot(it.parsed.rootMidi!);
        return {
          file: it.targetPath,
          rootNote: it.parsed.rootNote!,
          lowKey: midiToName(lowMidi),
          highKey: midiToName(highMidi),
          velocityMin: 1,
          velocityMax: 127,
          gainDb: 0,
          tuningCents: 0,
        };
      });

    manifests.push({
      presetName,
      category,
      mappingMode: "auto_multisample",
      samples,
      warnings: [],
    });
  }

  return { manifests, globalWarnings, globalErrors, duplicates, missingRoots };
};
