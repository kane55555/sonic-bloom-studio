// POST /license-verify
// Body: { machineId: string }
import { z } from "npm:zod@3";
import { corsHeaders, json } from "../_shared/cors.ts";
import { authenticate, clientIp } from "../_shared/auth.ts";
import { audit, sha256 } from "../_shared/audit.ts";
import { issueLicenseToken } from "../_shared/license-token.ts";

const Body = z.object({ machineId: z.string().min(8).max(256) });

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") return new Response("ok", { headers: corsHeaders });
  if (req.method !== "POST") return json({ error: "method_not_allowed" }, 405);

  try {
    const ctx = await authenticate(req);
    const parsed = Body.safeParse(await req.json().catch(() => null));
    if (!parsed.success) return json({ error: parsed.error.flatten() }, 400);

    const ip = clientIp(req);
    const machineIdHash = await sha256(parsed.data.machineId);

    const { data: device } = await ctx.admin
      .from("device_activations")
      .select("id, is_active, revoked_at")
      .eq("user_id", ctx.userId).eq("machine_id", machineIdHash).maybeSingle();

    if (!device || !device.is_active) {
      await audit(ctx.admin, { userId: ctx.userId, eventType: "license_failed", machineId: machineIdHash, ip, metadata: { reason: "device_not_active" } });
      return json({ valid: false, reason: "device_not_active" }, 403);
    }

    const { data: profile } = await ctx.admin
      .from("profiles")
      .select("plan, subscription_status, max_devices, current_period_end")
      .eq("id", ctx.userId).maybeSingle();

    const subOk = profile && ["active", "trialing"].includes(profile.subscription_status);
    if (!subOk) {
      await audit(ctx.admin, { userId: ctx.userId, eventType: "license_failed", machineId: machineIdHash, ip, metadata: { reason: "subscription_inactive" } });
      return json({ valid: false, reason: "subscription_inactive", plan: profile?.plan }, 402);
    }

    // Refresh last_seen
    await ctx.admin.from("device_activations")
      .update({ last_seen_at: new Date().toISOString() })
      .eq("id", device.id);

    // Owned packs
    const { data: owned } = await ctx.admin
      .from("user_packs").select("pack_id").eq("user_id", ctx.userId);

    const { token, expiresAt } = await issueLicenseToken({
      sub: ctx.userId, machineId: machineIdHash, plan: profile.plan,
    });

    await audit(ctx.admin, { userId: ctx.userId, eventType: "license_verified", machineId: machineIdHash, ip });

    return json({
      valid: true,
      plan: profile.plan,
      status: profile.subscription_status,
      maxDevices: profile.max_devices,
      ownedPacks: (owned ?? []).map((o) => o.pack_id),
      expiresAt,
      licenseToken: token,
      currentPeriodEnd: profile.current_period_end,
    });
  } catch (e) {
    if (e instanceof Response) return e;
    return json({ error: "internal_error", detail: String(e) }, 500);
  }
});
