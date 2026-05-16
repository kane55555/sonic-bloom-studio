import { z } from "zod";
import { requireSupabase } from "../supabaseClient";
import { userRowSchema, type UserRow, appRole } from "../schemas";

export async function listUsers(): Promise<UserRow[]> {
  const sb = requireSupabase();
  const { data, error } = await sb
    .from("profiles")
    .select(`
      id, email, display_name, plan, subscription_status, max_devices,
      stripe_customer_id, current_period_end, created_at,
      user_roles ( role ),
      device_activations ( id, is_active )
    `)
    .order("created_at", { ascending: false });
  if (error) throw error;

  return z.array(userRowSchema).parse(
    (data ?? []).map((r: any) => ({
      ...r,
      role: r.user_roles?.[0]?.role ?? "user",
      device_count: (r.device_activations ?? []).filter((d: any) => d.is_active).length,
    })),
  );
}

export async function setUserRole(userId: string, role: z.infer<typeof appRole>) {
  const sb = requireSupabase();
  // Upsert into user_roles; never store roles on profiles.
  const { error } = await sb.from("user_roles").upsert(
    { user_id: userId, role },
    { onConflict: "user_id,role" },
  );
  if (error) throw error;
}

export async function setUserPlan(userId: string, plan: "free" | "basic" | "pro") {
  const sb = requireSupabase();
  const { error } = await sb.from("profiles").update({ plan }).eq("id", userId);
  if (error) throw error;
}
