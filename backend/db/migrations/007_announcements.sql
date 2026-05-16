-- 007_announcements.sql

CREATE TABLE IF NOT EXISTS public.announcements (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    title TEXT NOT NULL,
    body TEXT NOT NULL,
    severity TEXT NOT NULL DEFAULT 'info',
    target_plan public.sub_plan,
    is_published BOOLEAN NOT NULL DEFAULT FALSE,
    published_at TIMESTAMPTZ,
    expires_at TIMESTAMPTZ,
    created_by UUID REFERENCES auth.users(id) ON DELETE SET NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
ALTER TABLE public.announcements ENABLE ROW LEVEL SECURITY;
CREATE INDEX IF NOT EXISTS idx_ann_published ON public.announcements(is_published, published_at DESC);

DROP TRIGGER IF EXISTS tg_ann_updated_at ON public.announcements;
CREATE TRIGGER tg_ann_updated_at BEFORE UPDATE ON public.announcements
    FOR EACH ROW EXECUTE FUNCTION public.tg_set_updated_at();

DROP POLICY IF EXISTS "ann: public read" ON public.announcements;
DROP POLICY IF EXISTS "ann: admin all"   ON public.announcements;
CREATE POLICY "ann: public read" ON public.announcements
    FOR SELECT TO authenticated USING (
        is_published = TRUE AND (expires_at IS NULL OR expires_at > NOW())
    );
CREATE POLICY "ann: admin all" ON public.announcements
    FOR ALL TO authenticated USING (public.has_role(auth.uid(), 'admin'));
