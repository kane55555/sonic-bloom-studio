-- 001_enums.sql — Shared enums for DIDITAGAIN STUDIO backend

DO $$ BEGIN
    CREATE TYPE public.app_role AS ENUM ('admin', 'user');
EXCEPTION WHEN duplicate_object THEN NULL; END $$;

DO $$ BEGIN
    CREATE TYPE public.sub_plan AS ENUM ('free', 'basic', 'pro');
EXCEPTION WHEN duplicate_object THEN NULL; END $$;

DO $$ BEGIN
    CREATE TYPE public.sub_status AS ENUM ('active', 'past_due', 'canceled', 'expired', 'trialing');
EXCEPTION WHEN duplicate_object THEN NULL; END $$;

DO $$ BEGIN
    CREATE TYPE public.pack_instrument_type AS ENUM (
        'piano', 'keys', 'guitar', 'strings', 'pad', 'bell', 'pluck',
        'lead', 'bass', 'drum', 'fx', 'texture', 'vocal', 'other'
    );
EXCEPTION WHEN duplicate_object THEN NULL; END $$;

DO $$ BEGIN
    CREATE TYPE public.sample_mapping_mode AS ENUM ('one_shot', 'auto_multisample', 'full_multisample');
EXCEPTION WHEN duplicate_object THEN NULL; END $$;

CREATE OR REPLACE FUNCTION public.tg_set_updated_at()
RETURNS TRIGGER LANGUAGE plpgsql AS $$
BEGIN NEW.updated_at = NOW(); RETURN NEW; END;
$$;
