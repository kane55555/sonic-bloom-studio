/**
 * Manifest types shared between admin dashboard, edge functions, and the
 * native plugin. A "pack" ships as a manifest JSON + sample files.
 */

import { z } from "zod";

export const SampleZoneSchema = z.object({
  samplePath: z.string().min(1),           // relative to pack root
  rootNote: z.number().int().min(0).max(127),
  lowKey: z.number().int().min(0).max(127),
  highKey: z.number().int().min(0).max(127),
  velocityMin: z.number().int().min(1).max(127).default(1),
  velocityMax: z.number().int().min(1).max(127).default(127),
  loopStart: z.number().nonnegative().optional(),
  loopEnd: z.number().nonnegative().optional(),
  gainDb: z.number().default(0),
  tuningCents: z.number().default(0),
  tags: z.array(z.string()).default([]),
});
export type SampleZone = z.infer<typeof SampleZoneSchema>;

export const PresetEntrySchema = z.object({
  name: z.string().min(1),
  category: z.string(),
  instrumentType: z.string(),
  tags: z.array(z.string()).default([]),
  rootKeyMode: z.enum(["fixed", "tracking", "auto"]).default("tracking"),
  sampleMappingMode: z.enum(["one_shot", "auto_multisample", "full_multisample"]),
  requiredPackId: z.string().uuid().optional(),
  zones: z.array(SampleZoneSchema).min(1),
});
export type PresetEntry = z.infer<typeof PresetEntrySchema>;

export const MultisampleManifestSchema = z.object({
  manifestVersion: z.literal("1.0.0"),
  packId: z.string().uuid(),
  packSlug: z.string(),
  packName: z.string(),
  version: z.string(),
  checksumSha256: z.string(),
  generatedAt: z.string(),
  presets: z.array(PresetEntrySchema),
});
export type MultisampleManifest = z.infer<typeof MultisampleManifestSchema>;

/** Result helper for validation callers. */
export const validateManifest = (raw: unknown) =>
  MultisampleManifestSchema.safeParse(raw);
