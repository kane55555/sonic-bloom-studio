// GET /entitlements
// Returns the packs the authenticated user can access.
import { corsHeaders, json } from "../_shared/cors.ts";
import { authenticate } from "../_shared/auth.ts";

const PLAN_RANK: Record<string, number> = { free: 0, basic: 1, pro: 2 };

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") return new Response("ok", { headers: corsHeaders });
  if (req.method !== "GET" && req.method !== "POST") return json({ error: "method_not_allowed" }, 405);

  try {
    const ctx = await authenticate(req);

    const { data: profile } = await ctx.admin
      .from("profiles")
      .select("plan, subscription_status, max_devices")
      .eq("id", ctx.userId).maybeSingle();
    if (!profile) return json({ error: "no_profile" }, 404);

    const userRank = PLAN_RANK[profile.plan] ?? 0;

    const { data: owned } = await ctx.admin
      .from("user_packs").select("pack_id").eq("user_id", ctx.userId);
    const ownedIds = new Set((owned ?? []).map((o) => o.pack_id));

    const { data: packs } = await ctx.admin
      .from("preset_packs")
      .select(`
        id, slug, name, description, category, instrument_type,
        preset_count, file_size_mb, cover_image_url, manifest_url,
        required_plan, is_factory, version,
        pack_versions ( version, manifest_url, download_url, checksum_sha256, is_latest, created_at )
      `)
      .eq("is_published", true);

    const entitled = (packs ?? []).map((p) => {
      const planIncluded = (PLAN_RANK[p.required_plan as string] ?? 0) <= userRank;
      const purchased = ownedIds.has(p.id);
      const accessible = p.required_plan === "free" || planIncluded || purchased;
      const latest = (p.pack_versions ?? []).find((v: any) => v.is_latest)
        ?? (p.pack_versions ?? []).slice().sort((a: any, b: any) => (a.created_at < b.created_at ? 1 : -1))[0]
        ?? null;
      return {
        id: p.id, slug: p.slug, name: p.name, description: p.description,
        category: p.category, instrumentType: p.instrument_type,
        presetCount: p.preset_count, fileSizeMb: p.file_size_mb,
        coverImageUrl: p.cover_image_url,
        requiredPlan: p.required_plan, isFactory: p.is_factory,
        accessible,
        accessSource: purchased ? "purchase" : planIncluded ? "plan" : "public",
        latestVersion: latest ? {
          version: latest.version,
          manifestUrl: latest.manifest_url,
          downloadUrl: accessible ? latest.download_url : null,
          checksumSha256: latest.checksum_sha256,
        } : null,
      };
    });

    return json({
      plan: profile.plan,
      status: profile.subscription_status,
      maxDevices: profile.max_devices,
      ownedPackIds: [...ownedIds],
      packs: entitled,
    });
  } catch (e) {
    if (e instanceof Response) return e;
    return json({ error: "internal_error", detail: String(e) }, 500);
  }
});
