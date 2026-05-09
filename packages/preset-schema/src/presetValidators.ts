/**
 * Lightweight runtime validators + legacy migration for HybridPresetV2.
 * Intentionally dependency-free so this package stays trivial to consume
 * from both the web admin and Node CLI tools.
 */
import type { HybridPresetV2, Layer, PresetCategory } from "./presetTypes";
import { ALL_CATEGORIES, SCHEMA_VERSION } from "./presetTypes";
import { buildPresetFromTemplate } from "./categoryTemplates";

export interface ValidationResult { ok: boolean; errors: string[]; }

const isNum = (x: unknown): x is number => typeof x === "number" && Number.isFinite(x);
const isStr = (x: unknown): x is string => typeof x === "string";
const isBool = (x: unknown): x is boolean => typeof x === "boolean";

export function validatePreset(p: unknown): ValidationResult {
  const errors: string[] = [];
  const push = (m: string) => errors.push(m);
  if (!p || typeof p !== "object") return { ok: false, errors: ["preset is not an object"] };
  const o = p as Record<string, unknown>;

  if (!isStr(o.schemaVersion)) push("schemaVersion missing");
  if (o.plugin !== "DIDITAGAIN STUDIO") push("plugin must be 'DIDITAGAIN STUDIO'");
  if (!isStr(o.presetId)) push("presetId missing");
  if (!isStr(o.name)) push("name missing");
  if (!ALL_CATEGORIES.includes(o.category as PresetCategory)) push(`category invalid: ${o.category}`);
  if (o.engine !== "hybrid") push("engine must be 'hybrid'");
  if (!Array.isArray(o.layers)) push("layers must be an array");
  else if ((o.layers as unknown[]).length === 0 || (o.layers as unknown[]).length > 4)
    push("layers must contain 1..4 entries");
  else (o.layers as unknown[]).forEach((l, i) => {
    const errs = validateLayer(l);
    errs.forEach(e => push(`layer[${i}]: ${e}`));
  });

  if (!o.globalFilter || typeof o.globalFilter !== "object") push("globalFilter missing");
  if (!o.effects || typeof o.effects !== "object") push("effects missing");
  if (!Array.isArray(o.macros)) push("macros must be an array");

  return { ok: errors.length === 0, errors };
}

function validateLayer(l: unknown): string[] {
  const errs: string[] = [];
  if (!l || typeof l !== "object") return ["not an object"];
  const o = l as Record<string, unknown>;
  if (!isStr(o.id)) errs.push("id missing");
  if (!isStr(o.type)) errs.push("type missing");
  if (!["sample","oscillator","noise","texture"].includes(String(o.type)))
    errs.push(`type invalid: ${o.type}`);
  if (!isBool(o.enabled)) errs.push("enabled missing");
  if (!isNum(o.volume)) errs.push("volume missing");
  if (!isNum(o.pan)) errs.push("pan missing");
  if (!o.ampEnvelope || typeof o.ampEnvelope !== "object") errs.push("ampEnvelope missing");
  if (o.type === "sample") {
    if (!isStr(o.source)) errs.push("sample.source missing");
    if (!isNum(o.rootMidi)) errs.push("sample.rootMidi missing");
  }
  return errs;
}

export function isLegacyPreset(p: unknown): boolean {
  if (!p || typeof p !== "object") return false;
  const o = p as Record<string, unknown>;
  if (o.schemaVersion === SCHEMA_VERSION) return false;
  // Legacy v1: has presetVersion (number) or sampler.instrument or oscA, no layers[].
  return !!(o.presetVersion || o.sampler || o.oscA) && !Array.isArray(o.layers);
}

/**
 * Migrate a legacy v1 preset into HybridPresetV2.
 * - If preset has `sampler.instrument`, becomes a single sample-layer preset
 *   pointing at Samples/<instrument>/ (resolved at runtime by the engine).
 * - Otherwise becomes a synth preset where Layer 1 is a saw oscillator and
 *   Layer 2 is a sine sub.
 */
export function migrateLegacyToV2(legacy: Record<string, unknown>): HybridPresetV2 {
  const cat = mapLegacyCategory(String(legacy.category ?? "Uncategorized"));
  const name = String(legacy.presetName ?? "Migrated Preset");
  const presetId = `legacy-${Date.now()}-${Math.random().toString(36).slice(2,8)}`;

  // Sample-based legacy preset?
  const sampler = legacy.sampler as { instrument?: string } | undefined;
  if (sampler?.instrument) {
    return buildPresetFromTemplate({
      presetId,
      name,
      category: cat,
      bank: "Factory",
      author: String(legacy.author ?? "DIDITAGAIN"),
      samplePathRel: `Samples/${sampler.instrument}`,
      metadataPathRel: "",
      originalFileName: String(sampler.instrument),
      rootNote: "C4",
      rootMidi: 60,
      rootNoteSource: "manual",
      pitchTracking: true,
      tags: Array.isArray(legacy.tags) ? (legacy.tags as string[]) : [],
    });
  }

  // Synth preset — build a 2-osc patch.
  const built = buildPresetFromTemplate({
    presetId, name, category: cat, bank: "Factory",
    author: String(legacy.author ?? "DIDITAGAIN"),
    samplePathRel: "", metadataPathRel: "",
    originalFileName: name,
    rootNote: "C4", rootMidi: 60, rootNoteSource: "manual",
    pitchTracking: true,
    tags: Array.isArray(legacy.tags) ? (legacy.tags as string[]) : [],
  });
  // Disable sample layer 1 (no source) and turn layer 2 into the main saw.
  built.layers[0].enabled = false;
  const l2 = built.layers[1];
  if (l2.type === "oscillator") {
    l2.enabled = true;
    l2.waveform = "saw";
    l2.volume = 0.7;
  }
  return built;
}

function mapLegacyCategory(raw: string): PresetCategory {
  const c = raw.toLowerCase();
  if (/(bell|glock|musicbox|kalimba|vibe|xyl)/.test(c)) return "DrillBells";
  if (/(piano|rhodes|wurli|keys|ep)/.test(c)) return "PainPianos";
  if (/(choir|vox|vocal|ahh|ooh)/.test(c)) return "ChoirsVox";
  if (/(guitar|nylon)/.test(c)) return "Guitars";
  if (/(pad|atmo|ambient|drone)/.test(c)) return "DarkPads";
  if (/(pluck)/.test(c)) return "Plucks";
  if (/(808|bass|sub)/.test(c)) return "Bass808";
  if (/(fx|riser|hit|impact)/.test(c)) return "FXRisers";
  if (/(texture|noise|vinyl)/.test(c)) return "Textures";
  if (/(lead|alien|brass|trumpet|sax|trombone)/.test(c)) return "AlienLeads";
  return "Uncategorized";
}
