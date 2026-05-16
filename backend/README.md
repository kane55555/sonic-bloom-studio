# DIDITAGAIN STUDIO — Backend

Production backend for the DIDITAGAIN STUDIO sample-based FL Studio plugin.
Built on Supabase (auth, Postgres, Storage, Edge Functions).

## Layout

```
backend/
  db/
    schema.sql           # legacy single-file schema (reference only)
    migrations/          # ordered migration files (the source of truth)
      001_enums.sql
      002_profiles_roles.sql
      003_devices_licenses.sql
      004_preset_packs.sql
      005_subscriptions_stripe.sql
      006_audit_logs.sql
      007_announcements.sql
      008_storage_policies.sql
  api/
    license-service.ts   # legacy stub kept for reference (functions live in /supabase/functions)
  stripe/
    webhook-handler.ts   # legacy stub kept for reference
supabase/
  config.toml            # per-function JWT settings
  functions/
    _shared/             # cors, auth, audit, license-token helpers
    license-activate/
    license-verify/
    license-deactivate/
    entitlements/
    stripe-webhook/      # stub: requires STRIPE_SECRET_KEY + STRIPE_WEBHOOK_SECRET to fully activate
packages/preset-schema/
  src/
    manifestTypes.ts     # Zod schemas for MultisampleManifest / SampleZone
    entitlementTypes.ts  # shared response types
docs/
  backend-architecture.md
  preset-pack-manifest.md
  license-flow.md
  stripe-flow.md
```

## Enabling Cloud

1. From the Lovable chat, ask the agent to enable Lovable Cloud.
2. Copy `backend/db/migrations/*.sql` into `supabase/migrations/` and apply.
3. The Edge Functions under `supabase/functions/` deploy automatically.
4. Add secrets:
   - `LICENSE_SIGNING_SECRET` (long random string, required)
   - `STRIPE_SECRET_KEY` + `STRIPE_WEBHOOK_SECRET` (optional, only when wiring Stripe)
5. Grant admin to your account: `INSERT INTO public.user_roles (user_id, role) VALUES ('<auth.uid>', 'admin');`

## Conventions

- Roles **must** live in `public.user_roles`. Never store roles on `profiles`.
- All RLS policies use the `public.has_role()` security-definer helper.
- Edge Functions validate the JWT in code via `getClaims()` and use a
  service-role client for privileged writes (audit logs, device upserts).
- Every privileged action writes a row to `license_audit_log`.
- Hardware/machine IDs are stored **hashed** (SHA-256) — never raw.
