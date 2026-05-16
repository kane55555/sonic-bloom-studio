import { z } from "zod";
import { requireSupabase } from "../supabaseClient";
import {
  announcementSchema, announcementInputSchema,
  type Announcement, type AnnouncementInput,
} from "../schemas";

export async function listAnnouncements(): Promise<Announcement[]> {
  const sb = requireSupabase();
  const { data, error } = await sb
    .from("announcements").select("*").order("created_at", { ascending: false });
  if (error) throw error;
  return z.array(announcementSchema).parse(data ?? []);
}

export async function createAnnouncement(input: AnnouncementInput): Promise<Announcement> {
  const parsed = announcementInputSchema.parse(input);
  const sb = requireSupabase();
  const row = {
    ...parsed,
    published_at: parsed.status === "published" ? new Date().toISOString() : null,
  };
  const { data, error } = await sb
    .from("announcements").insert(row).select().single();
  if (error) throw error;
  return announcementSchema.parse(data);
}

export async function updateAnnouncement(id: string, patch: Partial<AnnouncementInput>) {
  const sb = requireSupabase();
  const { error } = await sb.from("announcements").update(patch).eq("id", id);
  if (error) throw error;
}

export async function deleteAnnouncement(id: string) {
  const sb = requireSupabase();
  const { error } = await sb.from("announcements").delete().eq("id", id);
  if (error) throw error;
}
