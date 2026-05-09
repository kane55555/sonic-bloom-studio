/**
 * Default 4-layer preset templates, one per producer-facing category.
 * `applyTemplate()` deep-clones a template and stamps in the imported
 * sample as Layer 1.
 */
import type {
  HybridPresetV2, PresetCategory, Effects, GlobalFilter, Macro, Layer,
  AmpEnvelope, SampleLayer, OscillatorLayer, NoiseLayer,
} from "./presetTypes";
import { SCHEMA_VERSION } from "./presetTypes";

const env = (a: number, d: number, s: number, r: number): AmpEnvelope =>
  ({ attack: a, decay: d, sustain: s, release: r });

const fx = (over: Partial<Effects> = {}): Effects => ({
  eq:         { enabled: true, lowCut: 80, body: 0,    presence: 0,    air: 0 },
  saturation: { enabled: false, mode: "tape", drive: 0.1, mix: 0.25 },
  chorus:     { enabled: false, rate: 0.3, depth: 0.2, mix: 0.2 },
  delay:      { enabled: false, sync: true, time: "1/4", feedback: 0.25, mix: 0.15 },
  reverb:     { enabled: true,  size: 0.5, decay: 2.0, mix: 0.2 },
  width:      { enabled: true,  amount: 0.3 },
  limiter:    { enabled: true,  ceiling: -0.5 },
  ...over,
});

const gf = (cutoff = 9000, type: GlobalFilter["type"] = "lowpass"): GlobalFilter =>
  ({ enabled: true, type, cutoff, resonance: 0.15, drive: 0.05 });

const macros = (m: Array<[string, number, Macro["targets"]]>): Macro[] =>
  m.map(([name, value, targets], i) => ({ id: `macro_${i+1}`, name, value, targets }));

/** Layer factories — id/name only; per-category templates fill the rest. */
const sampleLayer = (over: Partial<SampleLayer> = {}): SampleLayer => ({
  id: "layer_1",
  name: "Main Sample",
  type: "sample",
  enabled: true,
  source: "",
  rootNote: "C5",
  rootMidi: 72,
  pitchTracking: true,
  oneShotMode: false,
  volume: 0.82,
  pan: 0,
  pitch: 0,
  fineTune: 0,
  startOffset: 0,
  reverse: false,
  loop: false,
  ampEnvelope: env(0.001, 1.2, 0.15, 1.8),
  filter: { enabled: true, type: "lowpass", cutoff: 8500, resonance: 0.12, drive: 0.05 },
  ...over,
});

const oscLayer = (over: Partial<OscillatorLayer> = {}): OscillatorLayer => ({
  id: "layer_2",
  name: "Body",
  type: "oscillator",
  enabled: false,
  waveform: "sine",
  pitch: 0,
  fineTune: 0,
  volume: 0.2,
  pan: 0,
  ampEnvelope: env(0.001, 0.8, 0.1, 1.0),
  ...over,
});

const noiseLayer = (over: Partial<NoiseLayer> = {}): NoiseLayer => ({
  id: "layer_3",
  name: "Air",
  type: "noise",
  enabled: false,
  noiseType: "white",
  volume: 0.04,
  pan: 0,
  ampEnvelope: env(0.001, 0.05, 0, 0.02),
  ...over,
});

type Template = Pick<HybridPresetV2, "layers" | "globalFilter" | "effects" | "macros">;

const M_DARK = (): Macro["targets"] => [{ path: "globalFilter.cutoff", min: 3500, max: 12000 }];
const M_SPACE = (): Macro["targets"] => [
  { path: "effects.reverb.mix", min: 0, max: 0.55 },
  { path: "effects.delay.mix",  min: 0, max: 0.35 },
];
const M_GRIT = (): Macro["targets"] => [{ path: "effects.saturation.drive", min: 0, max: 0.45 }];
const M_WIDTH = (): Macro["targets"] => [
  { path: "effects.width.amount", min: 0, max: 0.75 },
  { path: "effects.chorus.mix",   min: 0, max: 0.45 },
];

export const CATEGORY_TEMPLATES: Record<PresetCategory, Template> = {
  DrillBells: {
    layers: [
      sampleLayer({ ampEnvelope: env(0.001, 1.2, 0.15, 1.8),
        filter: { enabled: true, type: "lowpass", cutoff: 8500, resonance: 0.12, drive: 0.05 } }),
      oscLayer({ id: "layer_2", name: "Sine Body", waveform: "sine", pitch: -12, volume: 0.22,
        enabled: true, ampEnvelope: env(0.001, 0.85, 0.05, 1.1) }),
      noiseLayer({ id: "layer_3", name: "Air Texture" }),
      oscLayer({ id: "layer_4", name: "Shimmer", waveform: "triangle", pitch: 12, fineTune: 7,
        volume: 0.08, ampEnvelope: env(0.02, 1.5, 0.12, 2.0) }),
    ],
    globalFilter: gf(9000),
    effects: fx({
      saturation: { enabled: true, mode: "tape", drive: 0.12, mix: 0.35 },
      chorus:     { enabled: true, rate: 0.35, depth: 0.18, mix: 0.22 },
      delay:      { enabled: true, sync: true, time: "1/4", feedback: 0.24, mix: 0.12 },
      reverb:     { enabled: true, size: 0.72, decay: 2.8, mix: 0.28 },
    }),
    macros: macros([
      ["Darkness", 0.5, M_DARK()],
      ["Space",    0.5, M_SPACE()],
      ["Grit",     0.25, M_GRIT()],
      ["Width",    0.5, M_WIDTH()],
    ]),
  },

  AlienLeads: {
    layers: [
      sampleLayer({ ampEnvelope: env(0.005, 0.4, 0.7, 0.6) }),
      oscLayer({ id: "layer_2", name: "Square Support", waveform: "square", enabled: true,
        volume: 0.25, ampEnvelope: env(0.005, 0.4, 0.7, 0.6) }),
      noiseLayer({}),
      oscLayer({ id: "layer_4", name: "Saw Detune", waveform: "saw", fineTune: 9, volume: 0.18 }),
    ],
    globalFilter: gf(11000),
    effects: fx({
      chorus: { enabled: true, rate: 0.4, depth: 0.25, mix: 0.3 },
      delay:  { enabled: true, sync: true, time: "1/8", feedback: 0.3, mix: 0.18 },
      reverb: { enabled: true, size: 0.6, decay: 2.2, mix: 0.22 },
    }),
    macros: macros([
      ["Glide", 0.2, [{ path: "globalFilter.cutoff", min: 8000, max: 14000 }]],
      ["Bite",  0.3, M_GRIT()],
      ["Space", 0.5, M_SPACE()],
      ["Width", 0.5, M_WIDTH()],
    ]),
  },

  PainPianos: {
    layers: [
      sampleLayer({ ampEnvelope: env(0.002, 1.5, 0.6, 2.4),
        filter: { enabled: true, type: "lowpass", cutoff: 7500, resonance: 0.08, drive: 0 } }),
      oscLayer({ id: "layer_2", name: "Pad Under", waveform: "triangle", pitch: -12, volume: 0.1 }),
      noiseLayer({}), oscLayer({ id: "layer_4", name: "Shimmer", pitch: 12, volume: 0 }),
    ],
    globalFilter: gf(7800),
    effects: fx({
      saturation: { enabled: true, mode: "tape", drive: 0.08, mix: 0.25 },
      reverb:     { enabled: true, size: 0.8, decay: 3.5, mix: 0.32 },
    }),
    macros: macros([
      ["Softness", 0.5, [{ path: "layers.0.filter.cutoff", min: 3500, max: 9000 }]],
      ["Room",     0.4, [{ path: "effects.reverb.mix", min: 0, max: 0.55 }]],
      ["Dark",     0.5, M_DARK()],
      ["Width",    0.4, M_WIDTH()],
    ]),
  },

  ChoirsVox: {
    layers: [
      sampleLayer({ ampEnvelope: env(0.4, 1.0, 0.85, 2.5) }),
      oscLayer({ id: "layer_2", name: "Shimmer", waveform: "triangle", pitch: 12, volume: 0.06 }),
      noiseLayer({}), oscLayer({ id: "layer_4", name: "Sub", pitch: -12, volume: 0 }),
    ],
    globalFilter: gf(8500),
    effects: fx({
      chorus: { enabled: true, rate: 0.25, depth: 0.2, mix: 0.25 },
      reverb: { enabled: true, size: 0.85, decay: 4.0, mix: 0.4 },
      width:  { enabled: true, amount: 0.6 },
    }),
    macros: macros([
      ["Air",      0.5, [{ path: "effects.eq.air", min: 0, max: 0.4 }]],
      ["Space",    0.5, M_SPACE()],
      ["Width",    0.5, M_WIDTH()],
      ["Darkness", 0.4, M_DARK()],
    ]),
  },

  Guitars: {
    layers: [
      sampleLayer({ ampEnvelope: env(0.002, 1.0, 0.4, 1.2) }),
      oscLayer({ id: "layer_2", name: "Body", waveform: "sine", pitch: -12, volume: 0 }),
      noiseLayer({}), oscLayer({ id: "layer_4", name: "Air" }),
    ],
    globalFilter: gf(10000),
    effects: fx({
      eq:     { enabled: true, lowCut: 120, body: 0.05, presence: 0.1, air: 0.12 },
      reverb: { enabled: true, size: 0.55, decay: 2.0, mix: 0.2 },
    }),
    macros: macros([
      ["Tone",     0.5, [{ path: "globalFilter.cutoff", min: 3000, max: 14000 }]],
      ["Room",     0.4, [{ path: "effects.reverb.mix", min: 0, max: 0.5 }]],
      ["Width",    0.4, M_WIDTH()],
      ["Softness", 0.4, M_DARK()],
    ]),
  },

  DarkPads: {
    layers: [
      sampleLayer({ ampEnvelope: env(0.6, 2.0, 0.85, 3.5) }),
      oscLayer({ id: "layer_2", name: "Sub", waveform: "sine", pitch: -12, volume: 0.18, enabled: true,
        ampEnvelope: env(0.6, 2.0, 0.85, 3.5) }),
      noiseLayer({ enabled: true, volume: 0.03,
        ampEnvelope: env(0.5, 2.0, 0.5, 2.0) }),
      oscLayer({ id: "layer_4", name: "Shimmer", waveform: "triangle", pitch: 12, volume: 0.1 }),
    ],
    globalFilter: gf(6500),
    effects: fx({
      chorus: { enabled: true, rate: 0.18, depth: 0.3, mix: 0.35 },
      reverb: { enabled: true, size: 0.9, decay: 5.0, mix: 0.45 },
      width:  { enabled: true, amount: 0.65 },
    }),
    macros: macros([
      ["Motion",  0.4, [{ path: "effects.chorus.depth", min: 0, max: 0.6 }]],
      ["Air",     0.5, [{ path: "effects.eq.air", min: 0, max: 0.4 }]],
      ["Space",   0.6, M_SPACE()],
      ["Darkness",0.5, M_DARK()],
    ]),
  },

  Plucks: {
    layers: [
      sampleLayer({ ampEnvelope: env(0.001, 0.35, 0.05, 0.6) }),
      oscLayer({ id: "layer_2", name: "Sine", waveform: "sine", pitch: -12, volume: 0.15 }),
      noiseLayer({}), oscLayer({ id: "layer_4", name: "Air", pitch: 12, volume: 0.05 }),
    ],
    globalFilter: gf(11000),
    effects: fx({
      delay:  { enabled: true, sync: true, time: "1/8", feedback: 0.28, mix: 0.18 },
      reverb: { enabled: true, size: 0.5, decay: 1.8, mix: 0.2 },
    }),
    macros: macros([
      ["Snap",       0.4, [{ path: "layers.0.ampEnvelope.attack", min: 0.001, max: 0.05 }]],
      ["Space",      0.5, M_SPACE()],
      ["Brightness", 0.6, [{ path: "globalFilter.cutoff", min: 4000, max: 16000 }]],
      ["Width",      0.4, M_WIDTH()],
    ]),
  },

  Bass808: {
    layers: [
      sampleLayer({ rootNote: "C2", rootMidi: 36,
        ampEnvelope: env(0.001, 1.0, 0.85, 0.8),
        filter: { enabled: true, type: "lowpass", cutoff: 4500, resonance: 0.1, drive: 0.1 } }),
      oscLayer({ id: "layer_2", name: "Sine Sub", waveform: "sine", pitch: 0, volume: 0.3, enabled: true,
        ampEnvelope: env(0.001, 1.0, 0.85, 0.8) }),
      noiseLayer({}), oscLayer({ id: "layer_4", name: "Distort Layer", waveform: "saw", volume: 0 }),
    ],
    globalFilter: gf(5000),
    effects: fx({
      saturation: { enabled: true, mode: "diode", drive: 0.3, mix: 0.4 },
      chorus:     { enabled: false, rate: 0, depth: 0, mix: 0 },
      reverb:     { enabled: false, size: 0, decay: 0, mix: 0 },
      width:      { enabled: false, amount: 0 },
    }),
    macros: macros([
      ["Drive", 0.3, [{ path: "effects.saturation.drive", min: 0, max: 0.7 }]],
      ["Glide", 0.0, [{ path: "globalFilter.cutoff", min: 2500, max: 8000 }]],
      ["Tone",  0.5, M_DARK()],
      ["Punch", 0.5, [{ path: "layers.0.ampEnvelope.decay", min: 0.3, max: 2.0 }]],
    ]),
  },

  FXRisers: {
    layers: [
      sampleLayer({ rootNote: "C4", rootMidi: 60, pitchTracking: false, oneShotMode: true,
        ampEnvelope: env(0.001, 4.0, 0.0, 3.0) }),
      oscLayer({ id: "layer_2", name: "Noise Wash", enabled: false }),
      noiseLayer({ enabled: false, ampEnvelope: env(0.5, 2.0, 0.4, 2.0) }),
      oscLayer({ id: "layer_4", name: "Sweep", waveform: "saw", volume: 0 }),
    ],
    globalFilter: gf(14000, "highpass"),
    effects: fx({
      reverb: { enabled: true, size: 0.95, decay: 6.0, mix: 0.5 },
      width:  { enabled: true, amount: 0.7 },
    }),
    macros: macros([
      ["Size",    0.6, [{ path: "effects.reverb.size", min: 0.3, max: 0.99 }]],
      ["Reverse", 0.0, [{ path: "layers.0.startOffset", min: 0, max: 1 }]],
      ["Space",   0.6, M_SPACE()],
      ["Tone",    0.5, [{ path: "globalFilter.cutoff", min: 800, max: 16000 }]],
    ]),
  },

  Textures: {
    layers: [
      sampleLayer({ rootNote: "C4", rootMidi: 60, pitchTracking: false, oneShotMode: false, loop: true,
        ampEnvelope: env(0.6, 1.0, 0.85, 2.5) }),
      oscLayer({ id: "layer_2", name: "Drone", waveform: "sine", volume: 0 }),
      noiseLayer({ enabled: true, volume: 0.04, ampEnvelope: env(0.5, 2.0, 0.4, 2.0) }),
      oscLayer({ id: "layer_4", name: "Air", pitch: 12, volume: 0 }),
    ],
    globalFilter: gf(9000),
    effects: fx({
      reverb: { enabled: true, size: 0.85, decay: 4.0, mix: 0.4 },
      width:  { enabled: true, amount: 0.6 },
    }),
    macros: macros([
      ["Texture", 0.5, [{ path: "layers.2.volume", min: 0, max: 0.2 }]],
      ["Space",   0.5, M_SPACE()],
      ["Width",   0.5, M_WIDTH()],
      ["Tone",    0.5, M_DARK()],
    ]),
  },

  Uncategorized: {
    layers: [
      sampleLayer({}),
      oscLayer({ id: "layer_2", name: "Body" }),
      noiseLayer({}),
      oscLayer({ id: "layer_4", name: "Aux" }),
    ],
    globalFilter: gf(10000),
    effects: fx({ reverb: { enabled: true, size: 0.5, decay: 2.0, mix: 0.18 } }),
    macros: macros([
      ["Darkness", 0.5, M_DARK()],
      ["Space",    0.4, M_SPACE()],
      ["Grit",     0.2, M_GRIT()],
      ["Width",    0.4, M_WIDTH()],
    ]),
  },
};

/** Deep-clone a category template into a fresh object. */
export function getTemplate(category: PresetCategory): {
  layers: Layer[]; globalFilter: GlobalFilter; effects: Effects; macros: Macro[];
} {
  return JSON.parse(JSON.stringify(CATEGORY_TEMPLATES[category]));
}

/** Build a new HybridPresetV2 from a category template + sample asset. */
export function buildPresetFromTemplate(args: {
  presetId: string;
  name: string;
  category: PresetCategory;
  bank?: "Factory" | "User";
  author?: string;
  samplePathRel: string;        // e.g. "Samples/Imported/DrillBells/Dark_Bell_C5.wav"
  metadataPathRel: string;
  originalFileName: string;
  rootNote: string;
  rootMidi: number;
  rootNoteSource: "filename" | "pitch-detect" | "guessed" | "manual";
  pitchTracking: boolean;
  oneShotMode?: boolean;
  needsReview?: boolean;
  tags?: string[];
}): HybridPresetV2 {
  const t = getTemplate(args.category);
  const layer1 = t.layers[0] as SampleLayer;
  layer1.source = args.samplePathRel;
  layer1.rootNote = args.rootNote;
  layer1.rootMidi = args.rootMidi;
  layer1.pitchTracking = args.pitchTracking;
  if (args.oneShotMode != null) layer1.oneShotMode = args.oneShotMode;

  const now = new Date().toISOString();
  return {
    schemaVersion: SCHEMA_VERSION,
    plugin: "DIDITAGAIN STUDIO",
    presetId: args.presetId,
    name: args.name,
    bank: args.bank ?? "User",
    category: args.category,
    subCategory: "Imported One-Shot",
    author: args.author ?? "User",
    dateCreated: now,
    dateModified: now,
    genre: [],
    mood: [],
    tags: args.tags ?? [args.category.toLowerCase(), "imported", "hybrid"],
    engine: "hybrid",
    sourceImport: {
      originalFileName: args.originalFileName,
      samplePath: args.samplePathRel,
      metadataPath: args.metadataPathRel,
      detectedRootNote: args.rootNote,
      rootMidi: args.rootMidi,
      rootNoteSource: args.rootNoteSource,
      confidence: args.rootNoteSource === "filename" ? 0.95 : 0.4,
      pitchTracking: args.pitchTracking,
    },
    quality: {
      gainNormalized: false,
      rootNoteVerified: args.rootNoteSource === "filename" || args.rootNoteSource === "manual",
      loopChecked: false,
      needsReview: !!args.needsReview,
      volumeBalanced: false,
    },
    layers: t.layers,
    globalFilter: t.globalFilter,
    effects: t.effects,
    macros: t.macros,
  };
}
