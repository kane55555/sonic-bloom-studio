import { describe, it, expect } from "vitest";
import {
  parseSampleFileName,
  routeFile,
  generateManifests,
  zoneForRoot,
  noteToMidi,
  midiToName,
  SUPPORTED_ROOTS,
} from "../src/multisampleRouter";

describe("parseSampleFileName", () => {
  it("parses Guitar_C3.wav", () => {
    const r = parseSampleFileName("Guitar_C3.wav");
    expect(r.ok).toBe(true);
    expect(r.instrumentName).toBe("Guitar");
    expect(r.rootNote).toBe("C3");
    expect(r.category).toBe("Guitars");
  });

  it("normalizes sharps: D#3, Dsharp3, Ds3", () => {
    for (const s of ["Guitar_D#3.wav", "Guitar_Dsharp3.wav", "Guitar_Ds3.wav"]) {
      const r = parseSampleFileName(s);
      expect(r.ok).toBe(true);
      expect(r.rootNote).toBe("D#3");
    }
  });

  it("supports multi-word names like Dark_Guitar_F#4.wav", () => {
    const r = parseSampleFileName("Dark_Guitar_F#4.wav");
    expect(r.instrumentName).toBe("Dark Guitar");
    expect(r.rootNote).toBe("F#4");
    expect(r.category).toBe("Guitars");
  });

  it("routes choir/aah/vox correctly", () => {
    expect(parseSampleFileName("Choir_Aah_C4.wav").category).toBe("Choirs");
    expect(parseSampleFileName("Vox_A4.wav").category).toBe("Choirs");
  });

  it("rejects non-wav", () => {
    expect(parseSampleFileName("Guitar_C3.mp3").ok).toBe(false);
  });

  it("warns on unsupported root", () => {
    const r = parseSampleFileName("Guitar_B3.wav");
    expect(r.ok).toBe(true);
    expect(r.warnings.length).toBeGreaterThan(0);
  });
});

describe("zoneForRoot", () => {
  it("C3 covers C3..C#3", () => {
    const z = zoneForRoot(noteToMidi("C3")!);
    expect(midiToName(z.lowMidi)).toBe("C3");
    expect(midiToName(z.highMidi)).toBe("C#3");
  });
  it("A2 covers G#2..B2", () => {
    const z = zoneForRoot(noteToMidi("A2")!);
    expect(midiToName(z.lowMidi)).toBe("G#2");
    expect(midiToName(z.highMidi)).toBe("B2");
  });
  it("D#3 covers D3..E3", () => {
    const z = zoneForRoot(noteToMidi("D#3")!);
    expect(midiToName(z.lowMidi)).toBe("D3");
    expect(midiToName(z.highMidi)).toBe("E3");
  });
});

describe("routeFile + generateManifests", () => {
  it("routes Guitar_C3.wav to Guitars/Guitar_C3.wav", () => {
    const r = routeFile("Guitar_C3.wav");
    expect(r.targetPath).toBe("Guitars/Guitar_C3.wav");
  });

  it("groups files into a preset and emits sorted zones", () => {
    const files = ["Guitar_C3.wav", "Guitar_D#3.wav", "Guitar_F#3.wav", "Guitar_A3.wav"]
      .map(routeFile);
    const result = generateManifests(files);
    expect(result.manifests).toHaveLength(1);
    const m = result.manifests[0];
    expect(m.presetName).toBe("Guitar");
    expect(m.category).toBe("Guitars");
    expect(m.samples.map((s) => s.rootNote)).toEqual(["C3", "D#3", "F#3", "A3"]);
    expect(result.duplicates).toHaveLength(0);
    // 4 of 16 grid notes present → missingRoots warns
    expect(result.missingRoots[0].missing.length).toBe(SUPPORTED_ROOTS.length - 4);
  });

  it("detects duplicate roots", () => {
    const files = ["Guitar_C3.wav", "Guitar_C3.wav"].map(routeFile);
    const r = generateManifests(files);
    expect(r.duplicates).toHaveLength(1);
    expect(r.duplicates[0].rootNote).toBe("C3");
  });
});
