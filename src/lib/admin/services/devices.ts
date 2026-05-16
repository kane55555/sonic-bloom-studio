import { z } from "zod";
import { requireSupabase } from "../supabaseClient";
import { deviceSchema, type Device } from "../schemas";

export interface DeviceWithEmail extends Device {
  email: string | null;
}

export async function listDevices(): Promise<DeviceWithEmail[]> {
  const sb = requireSupabase();
  const { data, error } = await sb
    .from("device_activations")
    .select("*, profiles!device_activations_user_id_fkey ( email )")
    .order("last_seen_at", { ascending: false });
  if (error) throw error;

  return (data ?? []).map((d: any) => ({
    ...deviceSchema.parse({ ...d, profiles: undefined }),
    email: d.profiles?.email ?? null,
  }));
}

export async function revokeDevice(deviceId: string) {
  const sb = requireSupabase();
  const { error } = await sb
    .from("device_activations")
    .update({ is_active: false, revoked_at: new Date().toISOString() })
    .eq("id", deviceId);
  if (error) throw error;
}

export async function reactivateDevice(deviceId: string) {
  const sb = requireSupabase();
  const { error } = await sb
    .from("device_activations")
    .update({ is_active: true, revoked_at: null })
    .eq("id", deviceId);
  if (error) throw error;
}
