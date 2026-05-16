-- 003_devices_licenses.sql — Device activations + issued license sessions

CREATE TABLE IF NOT EXISTS public.device_activations (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
    machine_id TEXT NOT NULL,
    machine_name TEXT,
    os_info TEXT,
    activated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    last_seen_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    revoked_at TIMESTAMPTZ,
    is_active BOOLEAN NOT NULL DEFAULT TRUE,
    UNIQUE (user_id, machine_id)
);
ALTER TABLE public.device_activations ENABLE ROW LEVEL SECURITY;
CREATE INDEX IF NOT EXISTS idx_devices_user    ON public.device_activations(user_id);
CREATE INDEX IF NOT EXISTS idx_devices_machine ON public.device_activations(machine_id);
CREATE INDEX IF NOT EXISTS idx_devices_active  ON public.device_activations(user_id) WHERE is_active = TRUE;

CREATE TABLE IF NOT EXISTS public.license_sessions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
    device_id UUID NOT NULL REFERENCES public.device_activations(id) ON DELETE CASCADE,
    token_hash TEXT NOT NULL UNIQUE,
    issued_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    expires_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ
);
ALTER TABLE public.license_sessions ENABLE ROW LEVEL SECURITY;
CREATE INDEX IF NOT EXISTS idx_sessions_user   ON public.license_sessions(user_id);
CREATE INDEX IF NOT EXISTS idx_sessions_device ON public.license_sessions(device_id);
CREATE INDEX IF NOT EXISTS idx_sessions_expiry ON public.license_sessions(expires_at);

DROP POLICY IF EXISTS "devices: self read" ON public.device_activations;
DROP POLICY IF EXISTS "devices: admin all" ON public.device_activations;
CREATE POLICY "devices: self read" ON public.device_activations
    FOR SELECT TO authenticated USING (user_id = auth.uid());
CREATE POLICY "devices: admin all" ON public.device_activations
    FOR ALL TO authenticated USING (public.has_role(auth.uid(), 'admin'));

DROP POLICY IF EXISTS "sessions: self read" ON public.license_sessions;
DROP POLICY IF EXISTS "sessions: admin all" ON public.license_sessions;
CREATE POLICY "sessions: self read" ON public.license_sessions
    FOR SELECT TO authenticated USING (user_id = auth.uid());
CREATE POLICY "sessions: admin all" ON public.license_sessions
    FOR ALL TO authenticated USING (public.has_role(auth.uid(), 'admin'));
