/**
 * Short-lived signed license token issued to the plugin after activation/verify.
 * Uses HMAC-SHA256 with LICENSE_SIGNING_SECRET (server-only).
 * Payload: { sub, machineId, plan, exp }
 */

const enc = new TextEncoder();
const b64url = (bytes: Uint8Array | ArrayBuffer) => {
  const u = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  return btoa(String.fromCharCode(...u)).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
};

async function getKey(): Promise<CryptoKey> {
  const secret = Deno.env.get("LICENSE_SIGNING_SECRET") ?? "dev-insecure-replace-me";
  return crypto.subtle.importKey(
    "raw",
    enc.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign", "verify"],
  );
}

export interface LicenseTokenClaims {
  sub: string;          // user id
  machineId: string;
  plan: string;
  iat: number;
  exp: number;
}

const TTL_SECONDS = 60 * 60 * 24 * 7; // 7 days

export async function issueLicenseToken(claims: Omit<LicenseTokenClaims, "iat" | "exp">) {
  const now = Math.floor(Date.now() / 1000);
  const payload: LicenseTokenClaims = { ...claims, iat: now, exp: now + TTL_SECONDS };
  const header = { alg: "HS256", typ: "LIC" };
  const h = b64url(enc.encode(JSON.stringify(header)));
  const p = b64url(enc.encode(JSON.stringify(payload)));
  const key = await getKey();
  const sig = await crypto.subtle.sign("HMAC", key, enc.encode(`${h}.${p}`));
  return { token: `${h}.${p}.${b64url(sig)}`, expiresAt: new Date(payload.exp * 1000).toISOString() };
}

export async function verifyLicenseToken(token: string): Promise<LicenseTokenClaims | null> {
  const parts = token.split(".");
  if (parts.length !== 3) return null;
  const [h, p, s] = parts;
  const key = await getKey();
  const sig = Uint8Array.from(atob(s.replace(/-/g, "+").replace(/_/g, "/")), (c) => c.charCodeAt(0));
  const ok = await crypto.subtle.verify("HMAC", key, sig, enc.encode(`${h}.${p}`));
  if (!ok) return null;
  const payload = JSON.parse(atob(p.replace(/-/g, "+").replace(/_/g, "/"))) as LicenseTokenClaims;
  if (payload.exp < Math.floor(Date.now() / 1000)) return null;
  return payload;
}
