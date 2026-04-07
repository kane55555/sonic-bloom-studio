-- DIDITAGAIN STUDIO — Supabase SQL Schema
-- Run this migration when Lovable Cloud is enabled

-- Roles enum
CREATE TYPE public.app_role AS ENUM ('admin', 'user');

-- Subscription plans
CREATE TYPE public.sub_plan AS ENUM ('free', 'basic', 'pro');
CREATE TYPE public.sub_status AS ENUM ('active', 'past_due', 'canceled', 'expired');

-- User profiles
CREATE TABLE public.profiles (
    id UUID PRIMARY KEY REFERENCES auth.users(id) ON DELETE CASCADE,
    email TEXT NOT NULL,
    display_name TEXT,
    plan sub_plan NOT NULL DEFAULT 'free',
    subscription_status sub_status NOT NULL DEFAULT 'active',
    stripe_customer_id TEXT,
    stripe_subscription_id TEXT,
    max_devices INTEGER NOT NULL DEFAULT 2,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

ALTER TABLE public.profiles ENABLE ROW LEVEL SECURITY;

-- User roles (separate table per security guidelines)
CREATE TABLE public.user_roles (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES auth.users(id) ON DELETE CASCADE NOT NULL,
    role app_role NOT NULL,
    UNIQUE (user_id, role)
);

ALTER TABLE public.user_roles ENABLE ROW LEVEL SECURITY;

-- Security definer function to check roles
CREATE OR REPLACE FUNCTION public.has_role(_user_id UUID, _role app_role)
RETURNS BOOLEAN
LANGUAGE sql STABLE SECURITY DEFINER SET search_path = public
AS $$
    SELECT EXISTS (
        SELECT 1 FROM public.user_roles
        WHERE user_id = _user_id AND role = _role
    )
$$;

-- Device activations
CREATE TABLE public.device_activations (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES auth.users(id) ON DELETE CASCADE NOT NULL,
    machine_id TEXT NOT NULL,
    machine_name TEXT,
    os_info TEXT,
    activated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    revoked_at TIMESTAMPTZ,
    is_active BOOLEAN NOT NULL DEFAULT TRUE,
    UNIQUE (user_id, machine_id)
);

ALTER TABLE public.device_activations ENABLE ROW LEVEL SECURITY;

-- Preset packs (official)
CREATE TABLE public.preset_packs (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name TEXT NOT NULL,
    description TEXT,
    preset_count INTEGER NOT NULL DEFAULT 0,
    tier sub_plan NOT NULL DEFAULT 'free',
    is_published BOOLEAN NOT NULL DEFAULT FALSE,
    download_url TEXT,
    version TEXT NOT NULL DEFAULT '1.0.0',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

ALTER TABLE public.preset_packs ENABLE ROW LEVEL SECURITY;

-- User owned packs (entitlements)
CREATE TABLE public.user_packs (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES auth.users(id) ON DELETE CASCADE NOT NULL,
    pack_id UUID REFERENCES public.preset_packs(id) ON DELETE CASCADE NOT NULL,
    purchased_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (user_id, pack_id)
);

ALTER TABLE public.user_packs ENABLE ROW LEVEL SECURITY;

-- License audit log
CREATE TABLE public.license_audit_log (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES auth.users(id) ON DELETE SET NULL,
    event_type TEXT NOT NULL, -- login_success, login_failed, device_activated, device_revoked, license_verified, etc.
    machine_id TEXT,
    ip_address TEXT,
    metadata JSONB,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

ALTER TABLE public.license_audit_log ENABLE ROW LEVEL SECURITY;

-- Announcements
CREATE TABLE public.announcements (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    title TEXT NOT NULL,
    body TEXT NOT NULL,
    is_published BOOLEAN NOT NULL DEFAULT FALSE,
    published_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

ALTER TABLE public.announcements ENABLE ROW LEVEL SECURITY;

-- RLS Policies

-- Profiles: users can read/update own profile; admins can read all
CREATE POLICY "Users can view own profile" ON public.profiles
    FOR SELECT TO authenticated USING (id = auth.uid());
CREATE POLICY "Users can update own profile" ON public.profiles
    FOR UPDATE TO authenticated USING (id = auth.uid());
CREATE POLICY "Admins can view all profiles" ON public.profiles
    FOR SELECT TO authenticated USING (public.has_role(auth.uid(), 'admin'));

-- Device activations: users see own; admins see all
CREATE POLICY "Users can view own activations" ON public.device_activations
    FOR SELECT TO authenticated USING (user_id = auth.uid());
CREATE POLICY "Users can insert own activations" ON public.device_activations
    FOR INSERT TO authenticated WITH CHECK (user_id = auth.uid());
CREATE POLICY "Admins can manage all activations" ON public.device_activations
    FOR ALL TO authenticated USING (public.has_role(auth.uid(), 'admin'));

-- Preset packs: published packs readable by all; admins manage
CREATE POLICY "Anyone can view published packs" ON public.preset_packs
    FOR SELECT TO authenticated USING (is_published = TRUE);
CREATE POLICY "Admins can manage packs" ON public.preset_packs
    FOR ALL TO authenticated USING (public.has_role(auth.uid(), 'admin'));

-- User packs: users see own
CREATE POLICY "Users can view own packs" ON public.user_packs
    FOR SELECT TO authenticated USING (user_id = auth.uid());

-- Audit log: admins only
CREATE POLICY "Admins can view audit log" ON public.license_audit_log
    FOR SELECT TO authenticated USING (public.has_role(auth.uid(), 'admin'));
CREATE POLICY "System can insert audit log" ON public.license_audit_log
    FOR INSERT TO authenticated WITH CHECK (TRUE);

-- Announcements: published readable by all; admins manage
CREATE POLICY "Anyone can view published announcements" ON public.announcements
    FOR SELECT TO authenticated USING (is_published = TRUE);
CREATE POLICY "Admins can manage announcements" ON public.announcements
    FOR ALL TO authenticated USING (public.has_role(auth.uid(), 'admin'));

-- Auto-create profile on signup
CREATE OR REPLACE FUNCTION public.handle_new_user()
RETURNS TRIGGER LANGUAGE plpgsql SECURITY DEFINER SET search_path = public
AS $$
BEGIN
    INSERT INTO public.profiles (id, email)
    VALUES (NEW.id, NEW.email);
    INSERT INTO public.user_roles (user_id, role)
    VALUES (NEW.id, 'user');
    RETURN NEW;
END;
$$;

CREATE TRIGGER on_auth_user_created
    AFTER INSERT ON auth.users
    FOR EACH ROW EXECUTE FUNCTION public.handle_new_user();
