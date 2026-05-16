/**
 * Zod schemas mirroring the rows returned by the migrations under
 * backend/db/migrations/. Used to validate every Supabase response in the
 * admin dashboard.
 */
import { z } from "zod";

export const planTier = z.enum(["free", "basic", "pro"]);
export const subscriptionStatus = z.enum([
  "active", "past_due", "canceled", "expired", "trialing",
]);
export const appRole = z.enum(["admin", "moderator", "user"]);

export const profileSchema = z.object({
  id: z.string().uuid(),
  email: z.string().email().nullable(),
  display_name: z.string().nullable(),
  plan: planTier,
  subscription_status: subscriptionStatus,
  max_devices: z.number().int().nonnegative(),
  stripe_customer_id: z.string().nullable().optional(),
  current_period_end: z.string().nullable().optional(),
  created_at: z.string(),
});
export type Profile = z.infer<typeof profileSchema>;

export const userRowSchema = profileSchema.extend({
  role: appRole.default("user"),
  device_count: z.number().int().nonnegative().default(0),
});
export type UserRow = z.infer<typeof userRowSchema>;

export const deviceSchema = z.object({
  id: z.string().uuid(),
  user_id: z.string().uuid(),
  machine_id: z.string(),
  machine_name: z.string().nullable(),
  os_info: z.string().nullable(),
  activated_at: z.string(),
  last_seen_at: z.string(),
  revoked_at: z.string().nullable(),
  is_active: z.boolean(),
});
export type Device = z.infer<typeof deviceSchema>;

export const packSchema = z.object({
  id: z.string().uuid(),
  slug: z.string(),
  name: z.string(),
  description: z.string().nullable(),
  category: z.string().nullable(),
  instrument_type: z.string(),
  preset_count: z.number().int().nonnegative(),
  file_size_mb: z.number().nonnegative(),
  cover_image_url: z.string().nullable(),
  manifest_url: z.string().nullable(),
  required_plan: planTier,
  is_factory: z.boolean(),
  is_published: z.boolean(),
  version: z.string(),
  created_at: z.string(),
});
export type Pack = z.infer<typeof packSchema>;

export const packVersionSchema = z.object({
  id: z.string().uuid(),
  pack_id: z.string().uuid(),
  version: z.string(),
  manifest_url: z.string(),
  download_url: z.string().nullable(),
  checksum_sha256: z.string(),
  is_latest: z.boolean(),
  changelog: z.string().nullable(),
  created_at: z.string(),
});
export type PackVersion = z.infer<typeof packVersionSchema>;

export const userPackSchema = z.object({
  user_id: z.string().uuid(),
  pack_id: z.string().uuid(),
  granted_at: z.string(),
  source: z.enum(["purchase", "grant", "bundle"]).default("grant"),
});
export type UserPack = z.infer<typeof userPackSchema>;

export const announcementSchema = z.object({
  id: z.string().uuid(),
  title: z.string(),
  body: z.string(),
  status: z.enum(["draft", "published", "archived"]),
  published_at: z.string().nullable(),
  created_at: z.string(),
});
export type Announcement = z.infer<typeof announcementSchema>;

export const auditEventType = z.enum([
  "license_activated", "license_verified", "license_failed",
  "device_revoked", "subscription_updated", "entitlement_granted",
  "pack_published", "pack_unpublished", "admin_action",
  "login_success", "login_failed",
]);

export const auditLogSchema = z.object({
  id: z.string().uuid(),
  user_id: z.string().uuid().nullable(),
  event_type: auditEventType,
  machine_id: z.string().nullable(),
  ip_address: z.string().nullable(),
  user_agent: z.string().nullable(),
  metadata: z.record(z.unknown()).nullable(),
  created_at: z.string(),
});
export type AuditLog = z.infer<typeof auditLogSchema>;

// ---------- Mutation input schemas (used by forms) ----------

export const announcementInputSchema = z.object({
  title: z.string().trim().min(1).max(200),
  body: z.string().trim().min(1).max(5000),
  status: z.enum(["draft", "published", "archived"]).default("draft"),
});
export type AnnouncementInput = z.infer<typeof announcementInputSchema>;

export const packInputSchema = z.object({
  slug: z.string().trim().regex(/^[a-z0-9-]+$/, "lowercase, digits and hyphens only").max(64),
  name: z.string().trim().min(1).max(120),
  description: z.string().trim().max(2000).optional().nullable(),
  category: z.string().trim().max(64).optional().nullable(),
  instrument_type: z.string().trim().min(1).max(64),
  required_plan: planTier.default("free"),
  is_factory: z.boolean().default(false),
  is_published: z.boolean().default(false),
  version: z.string().trim().regex(/^\d+\.\d+\.\d+$/).default("1.0.0"),
  cover_image_url: z.string().url().optional().nullable(),
  manifest_url: z.string().url().optional().nullable(),
});
export type PackInput = z.infer<typeof packInputSchema>;

export const packVersionInputSchema = z.object({
  pack_id: z.string().uuid(),
  version: z.string().regex(/^\d+\.\d+\.\d+$/),
  manifest_url: z.string().url(),
  download_url: z.string().url().optional().nullable(),
  checksum_sha256: z.string().regex(/^[a-f0-9]{64}$/),
  is_latest: z.boolean().default(true),
  changelog: z.string().max(5000).optional().nullable(),
});
export type PackVersionInput = z.infer<typeof packVersionInputSchema>;

export const grantPackInputSchema = z.object({
  user_id: z.string().uuid(),
  pack_id: z.string().uuid(),
  source: z.enum(["purchase", "grant", "bundle"]).default("grant"),
});
export type GrantPackInput = z.infer<typeof grantPackInputSchema>;
