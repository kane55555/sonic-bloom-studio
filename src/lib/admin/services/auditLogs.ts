import { z } from "zod";
import { requireSupabase } from "../supabaseClient";
import { auditLogSchema, type AuditLog } from "../schemas";

export interface AuditFilters {
  eventType?: AuditLog["event_type"];
  userId?: string;
  limit?: number;
}

export async function listAuditLogs(filters: AuditFilters = {}): Promise<AuditLog[]> {
  const sb = requireSupabase();
  let q = sb.from("license_audit_log").select("*")
    .order("created_at", { ascending: false })
    .limit(filters.limit ?? 200);
  if (filters.eventType) q = q.eq("event_type", filters.eventType);
  if (filters.userId)    q = q.eq("user_id", filters.userId);
  const { data, error } = await q;
  if (error) throw error;
  return z.array(auditLogSchema).parse(data ?? []);
}
