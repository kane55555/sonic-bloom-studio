import { createClient, SupabaseClient } from "npm:@supabase/supabase-js@2";

export interface AuthedContext {
  userId: string;
  email: string;
  client: SupabaseClient;       // user-scoped (RLS applies)
  admin: SupabaseClient;        // service-role (bypasses RLS)
}

/** Validates the incoming JWT, returns the user id and two clients. */
export async function authenticate(req: Request): Promise<AuthedContext> {
  const authHeader = req.headers.get("Authorization");
  if (!authHeader?.startsWith("Bearer ")) throw new Response("Unauthorized", { status: 401 });

  const url = Deno.env.get("SUPABASE_URL")!;
  const anon = Deno.env.get("SUPABASE_ANON_KEY")!;
  const service = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!;

  const client = createClient(url, anon, {
    global: { headers: { Authorization: authHeader } },
  });

  const token = authHeader.replace("Bearer ", "");
  const { data, error } = await client.auth.getClaims(token);
  if (error || !data?.claims) throw new Response("Unauthorized", { status: 401 });

  const admin = createClient(url, service);
  return {
    userId: data.claims.sub as string,
    email: (data.claims.email as string) ?? "",
    client,
    admin,
  };
}

/** Best-effort client IP for audit logging. */
export const clientIp = (req: Request) =>
  req.headers.get("x-forwarded-for")?.split(",")[0]?.trim() ??
  req.headers.get("cf-connecting-ip") ?? "";
