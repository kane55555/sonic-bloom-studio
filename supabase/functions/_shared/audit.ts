import type { SupabaseClient } from "npm:@supabase/supabase-js@2";

export type AuditEvent =
  | "license_activated" | "license_verified" | "license_failed"
  | "device_revoked"    | "subscription_updated" | "entitlement_granted"
  | "pack_published"    | "pack_unpublished" | "admin_action"
  | "login_success"     | "login_failed";

export async function audit(
  admin: SupabaseClient,
  params: {
    userId?: string | null;
    eventType: AuditEvent;
    machineId?: string;
    ip?: string;
    userAgent?: string;
    metadata?: Record<string, unknown>;
  },
) {
  await admin.from("license_audit_log").insert({
    user_id: params.userId ?? null,
    event_type: params.eventType,
    machine_id: params.machineId ?? null,
    ip_address: params.ip ?? null,
    user_agent: params.userAgent ?? null,
    metadata: params.metadata ?? null,
  });
}

/** SHA-256 hex of arbitrary string (Web Crypto). */
export async function sha256(input: string): Promise<string> {
  const buf = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(input));
  return Array.from(new Uint8Array(buf)).map((b) => b.toString(16).padStart(2, "0")).join("");
}
