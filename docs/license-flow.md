# Licensing flow

## Concepts

- **Account** — Supabase auth user. Has one `profiles` row.
- **Device** — `(user_id, sha256(machineId))` row in `device_activations`.
- **License token** — short-lived HS256 JWT issued by the backend after
  activation/verify. Claims: `{ sub, machineId, plan, iat, exp }`.
  Default TTL: 7 days. Signed with `LICENSE_SIGNING_SECRET`.

## Plugin lifecycle

1. **Login** — plugin signs in with Supabase Auth → receives JWT.
2. **Activate** — `POST /license-activate { machineId, machineName, osInfo }`.
   - Checks plan & subscription status.
   - Rejects with `device_limit_reached` once `max_devices` is exceeded.
   - Upserts `device_activations`, mints license token.
3. **Verify** — `POST /license-verify { machineId }` on every cold start
   and every 24h while running.
   - Refreshes `last_seen_at`.
   - Returns updated `licenseToken` so the plugin can keep working
     offline for up to TTL.
4. **Deactivate** — `POST /license-deactivate { machineId }` either from
   the plugin's Settings tab or from the admin dashboard.

## Offline grace

The plugin caches the last good `licenseToken` and continues to function
until it expires (7 days). On every successful verify, the cache is
extended.

## Hardening (followups)

- Rate-limit `license-activate` / `license-verify` per IP and per user.
- Move HS256 signing key to a KMS-backed signer.
- Rotate `LICENSE_SIGNING_SECRET` quarterly.
