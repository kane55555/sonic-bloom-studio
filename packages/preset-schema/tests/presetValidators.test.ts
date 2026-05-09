import { describe, it, expect } from "vitest";
import {
  validatePreset,
  isLegacyPreset,
  migrateLegacyToV2,
} from "../src/presetValidators";
import { ALL_CATEGORIES, SCHEMA_VERSION } from "../src/presetTypes";
import { buildPresetFromTemplate } from "../src/categoryTemplates";

const buildValid = () =>
  buildPresetFromTemplate({
    presetId: "test-1",
    name: "Test Bell",
    category: "DrillBells",
    bank: "Factory",
    author: "Test",
    samplePathRel: "Samples/Imported/DrillBells/Bell.wav",
    metadataPathRel: "",
    originalFileName: "Bell.wav",
    rootNote: "C5",
    rootMidi: 72,
    rootNoteSource: "manual",
    pitchTracking: true,
    tags: ["bell"],
  });

describe("preset schema", () => {
  it("exposes 11 producer categories", () => {
    expect(ALL_CATEGORIES).toHaveLength(11);
    expect(ALL_CATEGORIES).toContain("DrillBells");
    expect(ALL_CATEGORIES).toContain("Bass808");
    expect(ALL_CATEGORIES).toContain("Uncategorized");
  });

  it("exposes schema version 2.0.0", () => {
    expect(SCHEMA_VERSION).toBe("2.0.0");
  });
});

describe("validatePreset", () => {
  it("accepts a freshly built template preset", () => {
    const p = buildValid();
    const r = validatePreset(p);
    expect(r.errors).toEqual([]);
    expect(r.ok).toBe(true);
  });

  it("rejects non-objects", () => {
    expect(validatePreset(null).ok).toBe(false);
    expect(validatePreset("nope" as unknown).ok).toBe(false);
  });

  it("rejects unknown plugin", () => {
    const p = buildValid() as Record<string, unknown>;
    p.plugin = "OTHER";
    const r = validatePreset(p);
    expect(r.ok).toBe(false);
    expect(r.errors.join()).toMatch(/plugin/);
  });

  it("rejects bad category", () => {
    const p = buildValid() as Record<string, unknown>;
    p.category = "NotARealCategory";
    expect(validatePreset(p).ok).toBe(false);
  });

  it("requires layers 1..4", () => {
    const p = buildValid() as Record<string, unknown>;
    p.layers = [];
    expect(validatePreset(p).ok).toBe(false);
  });

  it("flags missing sample fields on a sample layer", () => {
    const p = buildValid() as Record<string, unknown>;
    const layers = p.layers as Array<Record<string, unknown>>;
    delete layers[0].source;
    delete layers[0].rootMidi;
    const r = validatePreset(p);
    expect(r.ok).toBe(false);
    expect(r.errors.some(e => /source/.test(e))).toBe(true);
    expect(r.errors.some(e => /rootMidi/.test(e))).toBe(true);
  });
});

describe("legacy migration", () => {
  it("detects v1 sampler-based legacy preset", () => {
    expect(isLegacyPreset({ presetVersion: 1, sampler: { instrument: "Bell" } })).toBe(true);
    expect(isLegacyPreset({ schemaVersion: SCHEMA_VERSION, layers: [] })).toBe(false);
  });

  it("migrates a sampler legacy preset into a valid v2", () => {
    const v2 = migrateLegacyToV2({
      presetVersion: 1,
      presetName: "Old Bell",
      category: "Glockenspiel",
      sampler: { instrument: "Glock" },
    });
    expect(v2.schemaVersion).toBe(SCHEMA_VERSION);
    expect(v2.category).toBe("DrillBells");
    expect(validatePreset(v2).ok).toBe(true);
  });

  it("migrates a synth legacy preset into a valid v2", () => {
    const v2 = migrateLegacyToV2({
      presetVersion: 1,
      presetName: "Old Lead",
      category: "synthlead",
      oscA: { wave: "saw" },
    });
    expect(v2.category).toBe("AlienLeads");
    expect(validatePreset(v2).ok).toBe(true);
  });
});
