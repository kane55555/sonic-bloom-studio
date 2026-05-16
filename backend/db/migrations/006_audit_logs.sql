-- 006_audit_logs.sql

CREATE TABLE IF NOT EXISTS public.license_audit_log (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES auth.users(id) ON DELETE SET NULL,
    event_type TEXT NOT NULL,
    machine_id TEXT,
    ip_address TEXT,
    user_agent TEXT,
    metadata JSONB,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
ALTER TABLE public.license_audit_log ENABLE ROW LEVEL SECURITY;
CREATE INDEX IF NOT EXISTS idx_audit_user    ON public.license_audit_log(user_id);
CREATE INDEX IF NOT EXISTS idx_audit_type    ON public.license_audit_log(event_type);
CREATE INDEX IF NOT EXISTS idx_audit_created ON public.license_audit_log(created_at DESC);

DROP POLICY IF EXISTS "audit: admin read"    ON public.license_audit_log;
DROP POLICY IF EXISTS "audit: system insert" ON public.license_audit_log;
CREATE POLICY "audit: admin read" ON public.license_audit_log
    FOR SELECT TO authenticated USING (public.has_role(auth.uid(), 'admin'));
CREATE POLICY "audit: system insert" ON public.license_audit_log
    FOR INSERT TO authenticated WITH CHECK (TRUE);
