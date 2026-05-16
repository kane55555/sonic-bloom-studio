// POST /license-activate
// Body: { machineId: string, machineName?: string, osInfo?: string }
import { z } from "npm:zod@3";
import { corsHeaders, json } from "../_shared/cors.ts";
import { authenticate, clientIp } from "../_shared/auth.ts";
import { audit, sha256 } from "../_shared/audit.ts";
import { issueLicenseToken } from "../_shared/license-token.ts";

const Body = z.object({
  machineId: z.string().min(8).max(256),
  machineName: z.string().max(120).optional(),
  osInfo: z.string().max(200).optional(),
});

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") return new Response("ok", { headers: corsHeaders });
  if (req.method !== "POST") return json({ error: "method_not_allowed" }, 405);

  try {
    const ctx = await authenticate(req);
    const parsed = Body.safeParse(await req.json().catch(() => null));
    if (!parsed.success) return json({ error: parsed.error.flatten() }, 400);

    const ip = clientIp(req);
    const ua = req.headers.get("user-agent") ?? "";
    const machineIdHash = await sha256(parsed.data.machineId);

    // Subscription gate
    const { data: profile } = await ctx.admin
      .from("profiles")
      .select("plan, subscription_status, max_devices")
      .eq("id", ctx.userId).maybeSingle();
    if (!profile) return json({ error: "no_profile" }, 404);
    if (!["active", "trialing"].includes(profile.subscription_status)) {
      await audit(ctx.admin, { userId: ctx.userId, eventType: "license_failed", machineId: machineIdHash, ip, userAgent: ua, metadata: { reason: "subscription_inactive" } });
      return json({ error: "subscription_inactive" }, 402);
    }

    // Device limit
    const { count: activeCount } = await ctx.admin
      .from("device_activations")
      .select("id", { count: "exact", head: true })
      .eq("user_id", ctx.userId).eq("is_active", true);

    const { data: existing } = await ctx.admin
      .from("device_activations")
      .select("id, is_active")
      .eq("user_id", ctx.userId).eq("machine_id", machineIdHash).maybeSingle();

    if (!existing && (activeCount ?? 0) >= profile.max_devices) {
      await audit(ctx.admin, { userId: ctx.userId, eventType: "license_failed", machineId: machineIdHash, ip, userAgent: ua, metadata: { reason: "device_limit" } });
      return json({ error: "device_limit_reached", maxDevices: profile.max_devices }, 409);
    }

    const { data: device, error: upsertErr } = await ctx.admin
      .from("device_activations")
      .upsert({
        user_id: ctx.userId,
        machine_id: machineIdHash,
        machine_name: parsed.data.machineName,
        os_info: parsed.data.osInfo,
        is_active: true,
        revoked_at: null,
        last_seen_at: new Date().toISOString(),
      }, { onConflict: "user_id,machine_id" })
      .select("id").single();
    if (upsertErr) return json({ error: "activation_failed", detail: upsertErr.message }, 500);

    const { token, expiresAt } = await issueLicenseToken({
      sub: ctx.userId, machineId: machineIdHash, plan: profile.plan,
    });

    await ctx.admin.from("license_sessions").insert({
      user_id: ctx.userId, device_id: device.id,
      token_hash: await sha256(token), expires_at: expiresAt,
    });

    await audit(ctx.admin, { userId: ctx.userId, eventType: "license_activated", machineId: machineIdHash, ip, userAgent: ua });

    return json({ success: true, licenseToken: token, expiresAt, plan: profile.plan, maxDevices: profile.max_devices });
  } catch (e) {
    if (e instanceof Response) return e;
    return json({ error: "internal_error", detail: String(e) }, 500);
  }
});
