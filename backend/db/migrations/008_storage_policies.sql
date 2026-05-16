-- 008_storage_policies.sql — Apply AFTER enabling Lovable Cloud / Supabase Storage.

INSERT INTO storage.buckets (id, name, public) VALUES
    ('pack-covers',    'pack-covers',    TRUE),
    ('pack-manifests', 'pack-manifests', FALSE),
    ('pack-downloads', 'pack-downloads', FALSE)
ON CONFLICT (id) DO NOTHING;

DROP POLICY IF EXISTS "covers: public read" ON storage.objects;
DROP POLICY IF EXISTS "covers: admin write" ON storage.objects;
CREATE POLICY "covers: public read" ON storage.objects
    FOR SELECT USING (bucket_id = 'pack-covers');
CREATE POLICY "covers: admin write" ON storage.objects
    FOR ALL TO authenticated USING (
        bucket_id = 'pack-covers' AND public.has_role(auth.uid(), 'admin')
    );

DROP POLICY IF EXISTS "manifests: entitled read" ON storage.objects;
DROP POLICY IF EXISTS "manifests: admin write"   ON storage.objects;
CREATE POLICY "manifests: entitled read" ON storage.objects
    FOR SELECT TO authenticated USING (
        bucket_id = 'pack-manifests'
        AND (
            public.has_role(auth.uid(), 'admin')
            OR EXISTS (
                SELECT 1 FROM public.user_packs up
                JOIN public.preset_packs p ON p.id = up.pack_id
                WHERE up.user_id = auth.uid()
                  AND (storage.foldername(name))[1] = p.slug
            )
        )
    );
CREATE POLICY "manifests: admin write" ON storage.objects
    FOR ALL TO authenticated USING (
        bucket_id = 'pack-manifests' AND public.has_role(auth.uid(), 'admin')
    );

DROP POLICY IF EXISTS "downloads: admin read"  ON storage.objects;
DROP POLICY IF EXISTS "downloads: admin write" ON storage.objects;
CREATE POLICY "downloads: admin read" ON storage.objects
    FOR SELECT TO authenticated USING (
        bucket_id = 'pack-downloads' AND public.has_role(auth.uid(), 'admin')
    );
CREATE POLICY "downloads: admin write" ON storage.objects
    FOR ALL TO authenticated USING (
        bucket_id = 'pack-downloads' AND public.has_role(auth.uid(), 'admin')
    );
