// POST /license-deactivate
// Body: { machineId: string }
import { z } from "npm:zod@3";
import { corsHeaders, json } from "../_shared/cors.ts";
import { authenticate, clientIp } from "../_shared/auth.ts";
import { audit, sha256 } from "../_shared/audit.ts";

const Body = z.object({ machineId: z.string().min(8).max(256) });

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") return new Response("ok", { headers: corsHeaders });
  if (req.method !== "POST") return json({ error: "method_not_allowed" }, 405);

  try {
    const ctx = await authenticate(req);
    const parsed = Body.safeParse(await req.json().catch(() => null));
    if (!parsed.success) return json({ error: parsed.error.flatten() }, 400);

    const machineIdHash = await sha256(parsed.data.machineId);

    const { data: device, error } = await ctx.admin
      .from("device_activations")
      .update({ is_active: false, revoked_at: new Date().toISOString() })
      .eq("user_id", ctx.userId).eq("machine_id", machineIdHash)
      .select("id").maybeSingle();
    if (error) return json({ error: "revoke_failed", detail: error.message }, 500);
    if (!device) return json({ error: "not_found" }, 404);

    await ctx.admin.from("license_sessions")
      .update({ revoked_at: new Date().toISOString() })
      .eq("device_id", device.id).is("revoked_at", null);

    await audit(ctx.admin, {
      userId: ctx.userId, eventType: "device_revoked",
      machineId: machineIdHash, ip: clientIp(req),
    });
    return json({ success: true });
  } catch (e) {
    if (e instanceof Response) return e;
    return json({ error: "internal_error", detail: String(e) }, 500);
  }
});
