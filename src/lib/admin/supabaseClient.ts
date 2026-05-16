/**
 * Lazy Supabase client for the admin dashboard.
 *
 * Lovable Cloud is intentionally NOT enabled yet. When it is enabled, the
 * following environment variables (auto-injected by Lovable) will become
 * available and this client will start returning a real instance:
 *
 *   VITE_SUPABASE_URL
 *   VITE_SUPABASE_PUBLISHABLE_KEY  (anon publishable)
 *
 * Until then, `getSupabase()` returns null and `isCloudEnabled()` is false,
 * so every service/hook in this folder degrades to an empty/disabled state
 * instead of throwing.
 */
import { createClient, type SupabaseClient } from "@supabase/supabase-js";

let cached: SupabaseClient | null = null;

const url = import.meta.env.VITE_SUPABASE_URL as string | undefined;
const anon = import.meta.env.VITE_SUPABASE_PUBLISHABLE_KEY as string | undefined;

export const isCloudEnabled = (): boolean => Boolean(url && anon);

export const getSupabase = (): SupabaseClient | null => {
  if (!isCloudEnabled()) return null;
  if (!cached) {
    cached = createClient(url!, anon!, {
      auth: { persistSession: true, autoRefreshToken: true, storage: localStorage },
    });
  }
  return cached;
};

/** Throws a uniform error when callers need a client but Cloud is off. */
export const requireSupabase = (): SupabaseClient => {
  const c = getSupabase();
  if (!c) throw new CloudDisabledError();
  return c;
};

export class CloudDisabledError extends Error {
  constructor() {
    super("Lovable Cloud is not enabled yet. Enable it to load live data.");
    this.name = "CloudDisabledError";
  }
}
