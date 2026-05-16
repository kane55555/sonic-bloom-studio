# Backend architecture

## Components

- **Postgres (Supabase)** — single source of truth for profiles, roles,
  devices, license sessions, packs, versions, entitlements, audit log,
  Stripe events.
- **Supabase Auth** — issues JWTs consumed by both the admin dashboard
  (browser) and the native plugin.
- **Edge Functions (Deno)** — privileged operations; validate JWT, run
  business rules, write with service role.
- **Supabase Storage** — three buckets: `pack-covers` (public),
  `pack-manifests` (entitled read), `pack-downloads` (signed URLs only).
- **Stripe (deferred)** — webhook scaffolded; activation deferred per
  product decision.

## Request flow — plugin

```
[plugin] -- email/password --> Supabase Auth   ===> JWT
[plugin] -- JWT + machineId --> /license-activate  ===> licenseToken (HS256, 7d)
[plugin] -- JWT + machineId --> /license-verify    (on startup + every 24h)
[plugin] -- JWT             --> /entitlements      (pack list + download URLs)
```

Every call lands a row in `license_audit_log` for forensics.

## Request flow — admin dashboard

```
[browser] -- supabase-js --> tables (RLS-scoped)
[browser] -- supabase-js --> Storage (cover uploads, manifest uploads)
[browser] -- supabase-js --> Edge Functions (revoke, grant pack, publish)
```

Admins are identified via `public.has_role(auth.uid(), 'admin')`.

## Trust boundaries

| Boundary | Trust | Mechanism |
|---|---|---|
| Browser → Postgres | Untrusted | RLS + `has_role()` |
| Browser → Edge Function | JWT-verified | `getClaims()` |
| Edge Function → Postgres | Trusted | service role key (server-only) |
| Stripe → webhook | Signature-verified | `stripe.webhooks.constructEventAsync` |
| Plugin → Edge Function | JWT-verified | same as browser |

## Why the license token is separate from the Supabase JWT

The plugin needs a small, plugin-only credential that includes the
hashed `machineId` claim so we can detect a token being lifted to
another machine. The Supabase JWT alone has no device binding.
