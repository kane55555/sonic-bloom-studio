/**
 * DIDITAGAIN STUDIO — Hybrid Preset Schema v2
 *
 * A preset is a layered patch (up to 4 layers) plus a global filter,
 * effects chain, and 4 macro knobs. Layers can be sample, oscillator,
 * noise or texture sources.
 *
 * Backwards compatibility: anything authored under schemaVersion 1
 * (the old `sampler.instrument` + osc-only schema) is migrated by
 * `migrateLegacyToV2()` in ./presetValidators.
 */

export const SCHEMA_VERSION = "2.0.0";

export type LayerType = "sample" | "oscillator" | "noise" | "texture";
export type Waveform = "sine" | "triangle" | "saw" | "square" | "pulse" | "supersaw";
export type FilterType = "lowpass" | "highpass" | "bandpass" | "notch";
export type SaturationMode = "tape" | "tube" | "diode" | "fold";

export type PresetCategory =
  | "DrillBells" | "AlienLeads" | "PainPianos" | "ChoirsVox"
  | "Guitars"   | "DarkPads"   | "Plucks"     | "Bass808"
  | "FXRisers"  | "Textures"   | "Uncategorized";

export const ALL_CATEGORIES: PresetCategory[] = [
  "DrillBells","AlienLeads","PainPianos","ChoirsVox","Guitars",
  "DarkPads","Plucks","Bass808","FXRisers","Textures","Uncategorized",
];

export interface AmpEnvelope {
  attack: number;   // seconds
  decay: number;
  sustain: number;  // 0..1
  release: number;
}

export interface LayerFilter {
  enabled: boolean;
  type: FilterType;
  cutoff: number;   // Hz
  resonance: number;// 0..1
  drive: number;    // 0..1
}

export interface BaseLayer {
  id: string;
  name: string;
  type: LayerType;
  enabled: boolean;
  volume: number;   // 0..1
  pan: number;      // -1..1
  ampEnvelope: AmpEnvelope;
  filter?: LayerFilter;
}

export interface SampleLayer extends BaseLayer {
  type: "sample";
  source: string;            // relative path under DIDITAGAIN STUDIO/
  rootNote: string;          // "C5"
  rootMidi: number;
  pitchTracking: boolean;
  oneShotMode: boolean;
  pitch: number;             // semitones
  fineTune: number;          // cents
  startOffset: number;       // samples (or 0..1 fraction; we use samples)
  reverse: boolean;
  loop: boolean;
  // --- Crop / loop metadata (0..1 fractions of the audio file length) ---
  cropStart?: number;          // default 0.0
  cropEnd?: number;            // default 1.0
  loopStart?: number;          // default 0.2
  loopEnd?: number;            // default 0.95
  loopCrossfadeMs?: number;    // default 20
  autoLoop?: boolean;          // default true for melodic categories
}

/** Non-destructive crop/loop metadata stored alongside an imported sample. */
export interface SampleImportMetadata {
  cropStart: number;
  cropEnd: number;
  loopEnabled: boolean;
  loopStart: number;
  loopEnd: number;
  loopCrossfadeMs: number;
  autoLoop: boolean;
  oneShotMode: boolean;
  pitchTracking: boolean;
}

export interface OscillatorLayer extends BaseLayer {
  type: "oscillator";
  waveform: Waveform;
  pitch: number;
  fineTune: number;
}

export interface NoiseLayer extends BaseLayer {
  type: "noise";
  noiseType?: "white" | "pink";
}

export interface TextureLayer extends BaseLayer {
  type: "texture";
  source: string;
  loop: boolean;
}

export type Layer = SampleLayer | OscillatorLayer | NoiseLayer | TextureLayer;

export interface GlobalFilter extends LayerFilter {}

export interface EQ {
  enabled: boolean;
  lowCut: number;      // Hz
  body: number;        // -1..1
  presence: number;    // -1..1
  air: number;         // -1..1
}
export interface Saturation {
  enabled: boolean;
  mode: SaturationMode;
  drive: number;       // 0..1
  mix: number;         // 0..1
}
export interface Chorus {
  enabled: boolean;
  rate: number;
  depth: number;
  mix: number;
}
export interface Delay {
  enabled: boolean;
  sync: boolean;
  time: string | number; // "1/4" or seconds
  feedback: number;
  mix: number;
}
export interface Reverb {
  enabled: boolean;
  size: number;
  decay: number; // seconds
  mix: number;
}
export interface Width {
  enabled: boolean;
  amount: number; // 0..1
}
export interface Limiter {
  enabled: boolean;
  ceiling: number; // dBFS
}

export interface Effects {
  eq: EQ;
  saturation: Saturation;
  chorus: Chorus;
  delay: Delay;
  reverb: Reverb;
  width: Width;
  limiter: Limiter;
}

export interface MacroTarget {
  path: string;   // dotted path into preset (e.g. "globalFilter.cutoff")
  min: number;
  max: number;
}
export interface Macro {
  id: string;
  name: string;
  value: number;  // 0..1
  targets: MacroTarget[];
}

export type RootNoteSource = "filename" | "pitch-detect" | "guessed" | "manual";

export interface SourceImport {
  originalFileName: string;
  samplePath: string;
  metadataPath: string;
  detectedRootNote: string;
  rootMidi: number;
  rootNoteSource: RootNoteSource;
  confidence: number;       // 0..1
  pitchTracking: boolean;
}

export interface Quality {
  gainNormalized: boolean;
  rootNoteVerified: boolean;
  loopChecked: boolean;
  needsReview: boolean;
  volumeBalanced: boolean;
}

export interface HybridPresetV2 {
  schemaVersion: string;       // "2.0.0"
  plugin: "DIDITAGAIN STUDIO";
  presetId: string;            // uuid
  name: string;
  bank: "Factory" | "User";
  category: PresetCategory;
  subCategory?: string;
  author: string;
  dateCreated: string;         // ISO
  dateModified: string;        // ISO
  genre: string[];
  mood: string[];
  tags: string[];
  engine: "hybrid";
  sourceImport?: SourceImport;
  quality: Quality;
  layers: Layer[];             // <= 4
  globalFilter: GlobalFilter;
  effects: Effects;
  macros: Macro[];             // typically 4
}
