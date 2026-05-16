import { z } from "zod";
import { requireSupabase } from "../supabaseClient";
import {
  packSchema, packVersionSchema, packInputSchema, packVersionInputSchema,
  type Pack, type PackVersion, type PackInput, type PackVersionInput,
} from "../schemas";

// ---------- Packs ----------

export async function listPacks(): Promise<Pack[]> {
  const sb = requireSupabase();
  const { data, error } = await sb
    .from("preset_packs").select("*").order("created_at", { ascending: false });
  if (error) throw error;
  return z.array(packSchema).parse(data ?? []);
}

export async function createPack(input: PackInput): Promise<Pack> {
  const parsed = packInputSchema.parse(input);
  const sb = requireSupabase();
  const { data, error } = await sb
    .from("preset_packs").insert(parsed).select().single();
  if (error) throw error;
  return packSchema.parse(data);
}

export async function updatePack(id: string, patch: Partial<PackInput>): Promise<Pack> {
  const sb = requireSupabase();
  const { data, error } = await sb
    .from("preset_packs").update(patch).eq("id", id).select().single();
  if (error) throw error;
  return packSchema.parse(data);
}

export async function setPackPublished(id: string, published: boolean) {
  const sb = requireSupabase();
  const { error } = await sb
    .from("preset_packs").update({ is_published: published }).eq("id", id);
  if (error) throw error;
}

export async function deletePack(id: string) {
  const sb = requireSupabase();
  const { error } = await sb.from("preset_packs").delete().eq("id", id);
  if (error) throw error;
}

// ---------- Pack versions ----------

export async function listPackVersions(packId?: string): Promise<PackVersion[]> {
  const sb = requireSupabase();
  let q = sb.from("pack_versions").select("*").order("created_at", { ascending: false });
  if (packId) q = q.eq("pack_id", packId);
  const { data, error } = await q;
  if (error) throw error;
  return z.array(packVersionSchema).parse(data ?? []);
}

export async function createPackVersion(input: PackVersionInput): Promise<PackVersion> {
  const parsed = packVersionInputSchema.parse(input);
  const sb = requireSupabase();
  if (parsed.is_latest) {
    // Demote previous latest version for this pack first.
    await sb.from("pack_versions")
      .update({ is_latest: false })
      .eq("pack_id", parsed.pack_id).eq("is_latest", true);
  }
  const { data, error } = await sb
    .from("pack_versions").insert(parsed).select().single();
  if (error) throw error;
  return packVersionSchema.parse(data);
}

export async function deletePackVersion(id: string) {
  const sb = requireSupabase();
  const { error } = await sb.from("pack_versions").delete().eq("id", id);
  if (error) throw error;
}
