/**
 * Typed helpers for the Edge Functions defined under
 * supabase/functions/. The admin dashboard uses these to:
 *   - inspect / reissue a user's licenseToken
 *   - force-revoke a device
 *   - pull the entitlements payload for impersonated debugging
 *
 * When Lovable Cloud is disabled, every helper throws CloudDisabledError —
 * the React Query hooks turn that into a friendly empty state.
 */
import { getSupabase, CloudDisabledError } from "./supabaseClient";
import type {
  LicenseActivateResponse,
  LicenseVerifyResponse,
  PluginEntitlementsResponse,
} from "../../../packages/preset-schema/src/entitlementTypes";

type FnName = "license-activate" | "license-verify" | "license-deactivate" | "entitlements";

async function invoke<T>(name: FnName, body?: Record<string, unknown>): Promise<T> {
  const sb = getSupabase();
  if (!sb) throw new CloudDisabledError();
  const { data, error } = await sb.functions.invoke<T>(name, { body });
  if (error) throw error;
  return data as T;
}

export const licenseActivate = (machineId: string, machineName?: string, osInfo?: string) =>
  invoke<LicenseActivateResponse>("license-activate", { machineId, machineName, osInfo });

export const licenseVerify = (machineId: string) =>
  invoke<LicenseVerifyResponse>("license-verify", { machineId });

export const licenseDeactivate = (machineId: string) =>
  invoke<{ success: boolean }>("license-deactivate", { machineId });

export const fetchEntitlements = () =>
  invoke<PluginEntitlementsResponse>("entitlements");
