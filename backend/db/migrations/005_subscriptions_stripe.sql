-- 005_subscriptions_stripe.sql

CREATE TABLE IF NOT EXISTS public.plan_entitlements (
    plan public.sub_plan PRIMARY KEY,
    max_devices INTEGER NOT NULL,
    includes_tier public.sub_plan NOT NULL,
    monthly_price_usd NUMERIC(10,2) NOT NULL DEFAULT 0,
    yearly_price_usd  NUMERIC(10,2) NOT NULL DEFAULT 0
);
ALTER TABLE public.plan_entitlements ENABLE ROW LEVEL SECURITY;

INSERT INTO public.plan_entitlements (plan, max_devices, includes_tier, monthly_price_usd, yearly_price_usd) VALUES
    ('free',  1, 'free',  0,     0),
    ('basic', 2, 'basic', 9.99,  99),
    ('pro',   5, 'pro',   19.99, 199)
ON CONFLICT (plan) DO NOTHING;

DROP POLICY IF EXISTS "plan_ent: public read" ON public.plan_entitlements;
DROP POLICY IF EXISTS "plan_ent: admin all"   ON public.plan_entitlements;
CREATE POLICY "plan_ent: public read" ON public.plan_entitlements
    FOR SELECT TO authenticated USING (TRUE);
CREATE POLICY "plan_ent: admin all" ON public.plan_entitlements
    FOR ALL TO authenticated USING (public.has_role(auth.uid(), 'admin'));

CREATE TABLE IF NOT EXISTS public.stripe_events (
    id TEXT PRIMARY KEY,
    type TEXT NOT NULL,
    customer_id TEXT,
    subscription_id TEXT,
    payload JSONB NOT NULL,
    processed_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    status TEXT NOT NULL DEFAULT 'ok'
);
ALTER TABLE public.stripe_events ENABLE ROW LEVEL SECURITY;
CREATE INDEX IF NOT EXISTS idx_stripe_events_customer ON public.stripe_events(customer_id);
CREATE INDEX IF NOT EXISTS idx_stripe_events_sub      ON public.stripe_events(subscription_id);

DROP POLICY IF EXISTS "stripe_events: admin read" ON public.stripe_events;
CREATE POLICY "stripe_events: admin read" ON public.stripe_events
    FOR SELECT TO authenticated USING (public.has_role(auth.uid(), 'admin'));
