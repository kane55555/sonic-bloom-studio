/**
 * DIDITAGAIN STUDIO — .didasynthpreset schema
 * TypeScript type definitions for preset files
 */

export type WaveformType = "sine" | "triangle" | "saw" | "square" | "supersaw" | "wavetable";
export type FilterType = "LP12" | "LP24" | "HP12" | "HP24" | "BP" | "Notch";
export type NoiseType = "white" | "pink";
export type LFOShape = "sine" | "triangle" | "saw" | "square" | "sampleAndHold";
export type EngineMode = "subtractive" | "fm2op" | "fm4op" | "wavetable" | "layer";
export type PlayMode = "poly" | "mono" | "legato";

export interface OscillatorParams {
  waveform: WaveformType;
  level: number;        // 0.0 - 1.0
  detuneCents: number;  // -100 to 100
  octave: number;       // -3 to 3
  semitone: number;     // -12 to 12
  pulseWidth: number;   // 0.01 - 0.99
  unisonVoices: number; // 1 - 8
  unisonDetune: number; // 0.0 - 1.0
  unisonSpread: number; // 0.0 - 1.0
  wavetableIndex?: number;
}

export interface SubOscParams {
  enabled: boolean;
  level: number;
  octave: number; // -2, -1
}

export interface NoiseParams {
  type: NoiseType;
  level: number;
}

export interface FilterParams {
  type: FilterType;
  cutoff: number;       // 20 - 20000 Hz
  resonance: number;    // 0.0 - 1.0
  drive: number;        // 0.0 - 1.0
  envAmount: number;    // -1.0 to 1.0
  keyTrack: number;     // 0.0 - 1.0
}

export interface EnvelopeParams {
  attack: number;   // seconds
  decay: number;
  sustain: number;  // 0.0 - 1.0
  release: number;
}

export interface LFOParams {
  rate: number;       // Hz or sync division
  shape: LFOShape;
  sync: boolean;
  depth: number;      // 0.0 - 1.0
}

export interface ModRouting {
  source: string;
  destination: string;
  amount: number;     // -1.0 to 1.0
  bipolar: boolean;
}

export interface FxChainParams {
  chorusMix: number;
  chorusRate: number;
  chorusDepth: number;
  delayMix: number;
  delayTime: number;
  delayFeedback: number;
  delaySync: boolean;
  reverbMix: number;
  reverbSize: number;
  reverbDamping: number;
  phaserMix: number;
  phaserRate: number;
  distortionAmount: number;
  distortionType: "soft" | "hard" | "fold";
  compressorThreshold: number;
  compressorRatio: number;
  eqLow: number;
  eqMid: number;
  eqHigh: number;
  limiterCeiling: number;
}

export interface MacroKnob {
  label: string;
  value: number;
  mappings: Array<{
    paramId: string;
    amount: number;
  }>;
}

export interface DidaSynthPreset {
  presetVersion: string;
  presetName: string;
  author: string;
  category: string;
  tags: string[];
  description: string;
  engineMode: EngineMode;
  playMode: PlayMode;
  masterGain: number;     // dB
  polyphony: number;      // 1-16
  glideTime: number;      // seconds

  oscA: OscillatorParams;
  oscB: OscillatorParams;
  subOsc: SubOscParams;
  noise: NoiseParams;

  filter1: FilterParams;
  filter2: FilterParams;

  env1: EnvelopeParams;   // Amp
  env2: EnvelopeParams;   // Filter
  env3: EnvelopeParams;   // Mod

  lfo1: LFOParams;
  lfo2: LFOParams;

  modMatrix: ModRouting[];

  fxChain: FxChainParams;

  macroKnobs: [MacroKnob, MacroKnob, MacroKnob, MacroKnob, MacroKnob, MacroKnob, MacroKnob, MacroKnob];

  uiSkinRef: string;
  tuningRef: string;

  checksum: string;
  signature: string;
}
