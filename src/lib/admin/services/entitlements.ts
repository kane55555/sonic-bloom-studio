import { z } from "zod";
import { requireSupabase } from "../supabaseClient";
import {
  userPackSchema, grantPackInputSchema,
  type UserPack, type GrantPackInput,
} from "../schemas";

export interface EntitlementRow extends UserPack {
  user_email: string | null;
  pack_name: string;
  pack_slug: string;
}

export async function listEntitlements(): Promise<EntitlementRow[]> {
  const sb = requireSupabase();
  const { data, error } = await sb
    .from("user_packs")
    .select(`
      user_id, pack_id, granted_at, source,
      profiles!user_packs_user_id_fkey ( email ),
      preset_packs!user_packs_pack_id_fkey ( name, slug )
    `)
    .order("granted_at", { ascending: false });
  if (error) throw error;

  return (data ?? []).map((r: any) => ({
    ...userPackSchema.parse({
      user_id: r.user_id, pack_id: r.pack_id,
      granted_at: r.granted_at, source: r.source ?? "grant",
    }),
    user_email: r.profiles?.email ?? null,
    pack_name: r.preset_packs?.name ?? "(deleted)",
    pack_slug: r.preset_packs?.slug ?? "",
  }));
}

export async function grantPack(input: GrantPackInput) {
  const parsed = grantPackInputSchema.parse(input);
  const sb = requireSupabase();
  const { error } = await sb.from("user_packs").upsert(parsed, {
    onConflict: "user_id,pack_id",
  });
  if (error) throw error;
}

export async function revokePack(userId: string, packId: string) {
  const sb = requireSupabase();
  const { error } = await sb
    .from("user_packs").delete()
    .eq("user_id", userId).eq("pack_id", packId);
  if (error) throw error;
}
