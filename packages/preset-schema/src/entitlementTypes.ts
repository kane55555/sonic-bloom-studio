/**
 * Shared response shapes between the licensing edge functions and the
 * web admin / native plugin clients.
 */

export type PlanTier = "free" | "basic" | "pro";
export type SubscriptionStatus = "active" | "past_due" | "canceled" | "expired" | "trialing";

export interface DeviceActivation {
  id: string;
  machineId: string;           // hashed
  machineName?: string | null;
  osInfo?: string | null;
  activatedAt: string;
  lastSeenAt: string;
  revokedAt?: string | null;
  isActive: boolean;
}

export interface PackVersion {
  version: string;
  manifestUrl: string;
  downloadUrl: string | null;  // null when user is not entitled
  checksumSha256: string;
}

export interface PresetPack {
  id: string;
  slug: string;
  name: string;
  description?: string | null;
  category?: string | null;
  instrumentType: string;
  presetCount: number;
  fileSizeMb: number;
  coverImageUrl?: string | null;
  requiredPlan: PlanTier;
  isFactory: boolean;
  accessible: boolean;
  accessSource: "purchase" | "plan" | "public";
  latestVersion: PackVersion | null;
}

export interface PluginEntitlementsResponse {
  plan: PlanTier;
  status: SubscriptionStatus;
  maxDevices: number;
  ownedPackIds: string[];
  packs: PresetPack[];
}

export interface LicenseActivateResponse {
  success: true;
  licenseToken: string;
  expiresAt: string;
  plan: PlanTier;
  maxDevices: number;
}

export interface LicenseVerifyResponse {
  valid: boolean;
  reason?: string;
  plan?: PlanTier;
  status?: SubscriptionStatus;
  maxDevices?: number;
  ownedPacks?: string[];
  expiresAt?: string;
  licenseToken?: string;
  currentPeriodEnd?: string | null;
}
