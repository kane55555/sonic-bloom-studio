/**
 * License Verification Service Stub
 * Deploy as Supabase Edge Function
 */

interface ActivateRequest {
  machineId: string;
  machineName?: string;
  osInfo?: string;
}

interface VerifyRequest {
  machineId: string;
}

/**
 * POST /api/license/activate
 * Activates a device for the authenticated user.
 * Checks max device count, inserts activation record, logs event.
 */
export async function activateDevice(userId: string, body: ActivateRequest) {
  // TODO: Implementation with Supabase client
  // 1. Count active devices for user
  // 2. Check against max_devices in profiles
  // 3. Upsert device_activations
  // 4. Insert audit log entry
  // 5. Return signed license token (JWT with machineId claim)
  return { success: true, licenseToken: 'stub-token' };
}

/**
 * POST /api/license/verify
 * Verifies current device license is valid.
 */
export async function verifyLicense(userId: string, body: VerifyRequest) {
  // TODO: Implementation
  // 1. Check subscription status
  // 2. Check device is still active
  // 3. Return fresh signed license token
  return { valid: true, plan: 'pro', expiresAt: new Date(Date.now() + 7 * 86400000).toISOString() };
}

/**
 * POST /api/license/deactivate
 * Deactivates a device.
 */
export async function deactivateDevice(userId: string, machineId: string) {
  // TODO: Mark device as revoked in device_activations
  // Log audit event
  return { success: true };
}

/**
 * GET /api/license/entitlements
 * Returns owned packs and subscription info.
 */
export async function getEntitlements(userId: string) {
  // TODO: Query user_packs + profiles
  return {
    plan: 'pro',
    status: 'active',
    ownedPacks: [],
    maxDevices: 2,
    activeDevices: 1,
  };
}
