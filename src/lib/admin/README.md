# Admin data layer

Plain-file wiring for the admin dashboard. **Lovable Cloud is intentionally
not enabled yet** — every module here is shaped to light up the moment Cloud
is turned on and the migrations under `backend/db/migrations/` are applied,
without any further code changes in the React app.

## Layout

```
src/lib/admin/
  supabaseClient.ts       Lazy supabase-js client + isCloudEnabled()
  schemas.ts              Zod schemas for every row + every mutation input
  licenseFunctions.ts     Typed wrappers around the Edge Functions
  hooks.ts                React Query hooks (one per service)
  services/
    users.ts              profiles + user_roles
    devices.ts            device_activations
    packs.ts              preset_packs + pack_versions
    entitlements.ts       user_packs
    announcements.ts      announcements
    auditLogs.ts          license_audit_log
```

## Contract

- Every read goes through `requireSupabase()` and is parsed with Zod before
  it leaves the service layer. Pages never see unvalidated rows.
- Every write accepts a Zod-validated input type from `schemas.ts`. Forms
  reuse those schemas with `zodResolver`.
- Every hook returns React Query's `isLoading` / `error` / `data` so pages
  can wrap their bodies in `<AsyncBoundary/>`.
- When Cloud is off, `isCloudEnabled()` is false, queries don't fire, and
  `<AsyncBoundary/>` shows a single uniform "Cloud not enabled" panel.

## Enabling Cloud later

1. Enable Lovable Cloud (auto-creates Supabase project + env vars).
2. Apply the SQL in `backend/db/migrations/` in order.
3. Deploy the Edge Functions under `supabase/functions/`.
4. Add the first admin: `INSERT INTO public.user_roles (user_id, role) VALUES ('<uid>', 'admin');`
5. Refresh the dashboard — every page lights up automatically.
