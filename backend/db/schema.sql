/**
 * DEPRECATED — use `backend/db/migrations/` instead.
 *
 * This single-file schema is kept as a flat reference of the production
 * tables. The source of truth is the ordered migrations under
 * `backend/db/migrations/`, which are copied into `supabase/migrations/`
 * once Lovable Cloud is enabled.
 *
 * Apply order:
 *   001_enums.sql
 *   002_profiles_roles.sql
 *   003_devices_licenses.sql
 *   004_preset_packs.sql
 *   005_subscriptions_stripe.sql
 *   006_audit_logs.sql
 *   007_announcements.sql
 *   008_storage_policies.sql
 */
