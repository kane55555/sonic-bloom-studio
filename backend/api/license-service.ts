/**
 * DEPRECATED — superseded by Edge Functions under `supabase/functions/`.
 *
 * - activateDevice  -> supabase/functions/license-activate/index.ts
 * - verifyLicense   -> supabase/functions/license-verify/index.ts
 * - deactivateDevice-> supabase/functions/license-deactivate/index.ts
 * - getEntitlements -> supabase/functions/entitlements/index.ts
 *
 * Shared response types live in `@diditagain/preset-schema`
 * (entitlementTypes.ts). This file is kept only so existing imports
 * do not break; no code in this module is wired into the runtime.
 */
export {};
