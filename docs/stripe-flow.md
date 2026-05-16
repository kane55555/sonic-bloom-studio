# Stripe flow (deferred — stub only today)

The Stripe webhook function is scaffolded at
`supabase/functions/stripe-webhook/index.ts` but does **not** verify
signatures and **does not** require any Stripe key today. It will return
`501 stripe_not_configured` until `STRIPE_WEBHOOK_SECRET` is set.

## Activation checklist (when ready)

1. Add secrets: `STRIPE_SECRET_KEY`, `STRIPE_WEBHOOK_SECRET`.
2. In `supabase/functions/stripe-webhook/index.ts`, uncomment the
   `import Stripe from "npm:stripe@14"` block and the
   `stripe.webhooks.constructEventAsync` signature check.
3. Populate the `PRICE_TO_PLAN` map with your real `price_…` IDs.
4. Confirm `verify_jwt = false` for `stripe-webhook` in
   `supabase/config.toml` (already set).
5. Point the Stripe Dashboard webhook to
   `https://<project>.functions.supabase.co/stripe-webhook`.

## Events handled

| Event | Effect on `profiles` |
|---|---|
| `checkout.session.completed` | upsert customer/subscription/plan, set status `active` |
| `customer.subscription.created` | same as above |
| `customer.subscription.updated` | refresh plan/status/period_end |
| `customer.subscription.deleted` | plan → `free`, status → `canceled` |
| `invoice.payment_succeeded` | status → `active` |
| `invoice.payment_failed` | status → `past_due` |

## Idempotency

Every event id is recorded in `stripe_events`. Duplicate deliveries are
short-circuited with `{ received: true, duplicate: true }`.

## Audit

Every successful handler writes a `subscription_updated` row to
`license_audit_log`.
