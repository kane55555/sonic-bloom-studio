// POST /stripe-webhook   (STUB — not deployed yet, no STRIPE_SECRET_KEY required)
//
// This file scaffolds the Stripe webhook flow so it can be activated later
// without restructuring. To finish wiring it:
//   1. Add STRIPE_SECRET_KEY and STRIPE_WEBHOOK_SECRET via the secrets tool.
//   2. Set verify_jwt = false for this function in supabase/config.toml.
//   3. Uncomment the signature verification block below.
//
// See docs/stripe-flow.md for the full integration plan.

import { createClient } from "npm:@supabase/supabase-js@2";
import { corsHeaders, json } from "../_shared/cors.ts";

type StripeEventType =
  | "checkout.session.completed"
  | "customer.subscription.created"
  | "customer.subscription.updated"
  | "customer.subscription.deleted"
  | "invoice.payment_succeeded"
  | "invoice.payment_failed";

interface MinimalStripeEvent {
  id: string;
  type: StripeEventType | string;
  data: { object: Record<string, any> };
}

const PRICE_TO_PLAN: Record<string, "free" | "basic" | "pro"> = {
  // Fill in once Stripe products exist:
  // price_xxx_basic_monthly: "basic",
  // price_xxx_pro_monthly:   "pro",
};

function planFromSubscription(sub: any): "free" | "basic" | "pro" {
  const priceId = sub?.items?.data?.[0]?.price?.id;
  return PRICE_TO_PLAN[priceId] ?? "free";
}

const STATUS_MAP: Record<string, "active" | "past_due" | "canceled" | "expired" | "trialing"> = {
  active: "active", trialing: "trialing", past_due: "past_due",
  canceled: "canceled", unpaid: "past_due", incomplete: "past_due",
  incomplete_expired: "expired",
};

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") return new Response("ok", { headers: corsHeaders });
  if (req.method !== "POST") return json({ error: "method_not_allowed" }, 405);

  const url = Deno.env.get("SUPABASE_URL");
  const service = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY");
  const webhookSecret = Deno.env.get("STRIPE_WEBHOOK_SECRET");
  if (!url || !service) return json({ error: "supabase_not_configured" }, 503);

  // TODO: enable when STRIPE_SECRET_KEY is provided.
  // import Stripe from "npm:stripe@14";
  // const stripe = new Stripe(Deno.env.get("STRIPE_SECRET_KEY")!, { apiVersion: "2024-06-20" });
  // const sig = req.headers.get("stripe-signature")!;
  // const raw = await req.text();
  // let event: Stripe.Event;
  // try { event = await stripe.webhooks.constructEventAsync(raw, sig, webhookSecret!); }
  // catch (err) { return json({ error: "invalid_signature", detail: String(err) }, 400); }

  if (!webhookSecret) {
    return json({ error: "stripe_not_configured", note: "Set STRIPE_WEBHOOK_SECRET + STRIPE_SECRET_KEY and enable signature verification." }, 501);
  }

  const event = (await req.json()) as MinimalStripeEvent;
  const admin = createClient(url, service);

  // Idempotency
  const { data: seen } = await admin.from("stripe_events").select("id").eq("id", event.id).maybeSingle();
  if (seen) return json({ received: true, duplicate: true });

  try {
    switch (event.type) {
      case "checkout.session.completed":
      case "customer.subscription.created":
      case "customer.subscription.updated": {
        const sub = event.data.object;
        const customerId = sub.customer as string;
        const subscriptionId = (sub.id ?? sub.subscription) as string;
        const status = STATUS_MAP[sub.status] ?? "active";
        const plan = planFromSubscription(sub);
        const periodEnd = sub.current_period_end
          ? new Date(sub.current_period_end * 1000).toISOString() : null;

        // Bump max_devices from plan_entitlements
        const { data: ent } = await admin.from("plan_entitlements")
          .select("max_devices").eq("plan", plan).maybeSingle();

        await admin.from("profiles").update({
          stripe_customer_id: customerId,
          stripe_subscription_id: subscriptionId,
          plan, subscription_status: status,
          max_devices: ent?.max_devices ?? 2,
          current_period_end: periodEnd,
        }).eq("stripe_customer_id", customerId);
        break;
      }
      case "customer.subscription.deleted": {
        const sub = event.data.object;
        await admin.from("profiles").update({
          plan: "free", subscription_status: "canceled", max_devices: 1,
        }).eq("stripe_subscription_id", sub.id);
        break;
      }
      case "invoice.payment_failed": {
        const inv = event.data.object;
        await admin.from("profiles").update({ subscription_status: "past_due" })
          .eq("stripe_customer_id", inv.customer);
        break;
      }
      case "invoice.payment_succeeded": {
        const inv = event.data.object;
        await admin.from("profiles").update({ subscription_status: "active" })
          .eq("stripe_customer_id", inv.customer);
        break;
      }
    }

    await admin.from("stripe_events").insert({
      id: event.id, type: event.type,
      customer_id: event.data.object.customer ?? null,
      subscription_id: event.data.object.id ?? null,
      payload: event, status: "ok",
    });
    await admin.from("license_audit_log").insert({
      event_type: "subscription_updated",
      metadata: { stripe_event: event.type, event_id: event.id },
    });

    return json({ received: true });
  } catch (e) {
    await admin.from("stripe_events").insert({
      id: event.id, type: event.type, payload: event,
      status: "error",
    }).select();
    return json({ error: "handler_failed", detail: String(e) }, 500);
  }
});
