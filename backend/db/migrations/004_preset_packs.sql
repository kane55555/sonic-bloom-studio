-- 004_preset_packs.sql — Packs, versioned releases, user ownership

CREATE TABLE IF NOT EXISTS public.preset_packs (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    slug TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    description TEXT,
    category TEXT,
    instrument_type public.pack_instrument_type NOT NULL DEFAULT 'other',
    preset_count INTEGER NOT NULL DEFAULT 0,
    file_size_mb NUMERIC(10,2) NOT NULL DEFAULT 0,
    cover_image_url TEXT,
    manifest_url TEXT,
    required_plan public.sub_plan NOT NULL DEFAULT 'free',
    tier public.sub_plan NOT NULL DEFAULT 'free',
    is_factory BOOLEAN NOT NULL DEFAULT FALSE,
    is_published BOOLEAN NOT NULL DEFAULT FALSE,
    version TEXT NOT NULL DEFAULT '1.0.0',
    created_by UUID REFERENCES auth.users(id) ON DELETE SET NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
ALTER TABLE public.preset_packs ENABLE ROW LEVEL SECURITY;
CREATE INDEX IF NOT EXISTS idx_packs_published ON public.preset_packs(is_published) WHERE is_published;
CREATE INDEX IF NOT EXISTS idx_packs_required_plan ON public.preset_packs(required_plan);
DROP TRIGGER IF EXISTS tg_packs_updated_at ON public.preset_packs;
CREATE TRIGGER tg_packs_updated_at BEFORE UPDATE ON public.preset_packs
    FOR EACH ROW EXECUTE FUNCTION public.tg_set_updated_at();

CREATE TABLE IF NOT EXISTS public.pack_versions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    pack_id UUID NOT NULL REFERENCES public.preset_packs(id) ON DELETE CASCADE,
    version TEXT NOT NULL,
    changelog TEXT,
    manifest_url TEXT NOT NULL,
    download_url TEXT NOT NULL,
    checksum_sha256 TEXT NOT NULL,
    file_size_mb NUMERIC(10,2) NOT NULL DEFAULT 0,
    is_latest BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (pack_id, version)
);
ALTER TABLE public.pack_versions ENABLE ROW LEVEL SECURITY;
CREATE INDEX IF NOT EXISTS idx_pack_versions_pack   ON public.pack_versions(pack_id);
CREATE INDEX IF NOT EXISTS idx_pack_versions_latest ON public.pack_versions(pack_id) WHERE is_latest;

CREATE TABLE IF NOT EXISTS public.user_packs (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
    pack_id UUID NOT NULL REFERENCES public.preset_packs(id) ON DELETE CASCADE,
    purchased_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    source TEXT NOT NULL DEFAULT 'purchase',
    UNIQUE (user_id, pack_id)
);
ALTER TABLE public.user_packs ENABLE ROW LEVEL SECURITY;
CREATE INDEX IF NOT EXISTS idx_user_packs_user ON public.user_packs(user_id);
CREATE INDEX IF NOT EXISTS idx_user_packs_pack ON public.user_packs(pack_id);

DROP POLICY IF EXISTS "packs: public read" ON public.preset_packs;
DROP POLICY IF EXISTS "packs: admin all"   ON public.preset_packs;
CREATE POLICY "packs: public read" ON public.preset_packs
    FOR SELECT TO authenticated USING (is_published = TRUE);
CREATE POLICY "packs: admin all" ON public.preset_packs
    FOR ALL TO authenticated USING (public.has_role(auth.uid(), 'admin'));

DROP POLICY IF EXISTS "versions: public read" ON public.pack_versions;
DROP POLICY IF EXISTS "versions: admin all"   ON public.pack_versions;
CREATE POLICY "versions: public read" ON public.pack_versions
    FOR SELECT TO authenticated USING (
        EXISTS (SELECT 1 FROM public.preset_packs p WHERE p.id = pack_id AND p.is_published)
    );
CREATE POLICY "versions: admin all" ON public.pack_versions
    FOR ALL TO authenticated USING (public.has_role(auth.uid(), 'admin'));

DROP POLICY IF EXISTS "user_packs: self read" ON public.user_packs;
DROP POLICY IF EXISTS "user_packs: admin all" ON public.user_packs;
CREATE POLICY "user_packs: self read" ON public.user_packs
    FOR SELECT TO authenticated USING (user_id = auth.uid());
CREATE POLICY "user_packs: admin all" ON public.user_packs
    FOR ALL TO authenticated USING (public.has_role(auth.uid(), 'admin'));
