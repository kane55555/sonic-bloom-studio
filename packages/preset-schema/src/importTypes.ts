import type { PresetCategory, RootNoteSource, HybridPresetV2 } from "./presetTypes";

export interface ImportCandidate {
  originalPath: string;
  originalFileName: string;
  detectedCategory: PresetCategory;
  categoryConfidence: number;          // 0..1
  detectedRootNote: string;            // "C5"
  rootMidi: number;
  rootNoteSource: RootNoteSource;
  detectedVelocity?: number;           // 1..127
  targetSamplePath: string;            // relative
  targetPresetPath: string;            // relative
  template: PresetCategory;            // template to apply (== detectedCategory by default)
  presetName: string;
  tags: string[];
  oneShotMode: boolean;
  pitchTracking: boolean;
  needsReview: boolean;
  warnings: string[];
}

export interface ImportReviewItem extends ImportCandidate {
  /** UI-mutable copy of the candidate. */
  edited?: Partial<ImportCandidate>;
  selected: boolean;
}

export interface ImportSummary {
  imported: number;
  byCategory: Record<string, number>;
  skipped: number;
  needsReview: number;
  errors: { file: string; message: string }[];
}

export interface ImportResult {
  candidate: ImportCandidate;
  preset: HybridPresetV2;
  copiedTo: string;
  presetWrittenTo: string;
}
