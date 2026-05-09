# DIDITAGAIN STUDIO — Hybrid Preset Architecture v2

This refactor turns the current Nexus-style sample player into a hybrid preset workstation (70% sampler / 30% editable synth) with a 4-layer engine, macro system, import-review workflow, and a real preset library — without breaking what already works.

The work is large. I'll land it in phased commits so the repo stays buildable after each phase.

---

## Phase 1 — Schema & TypeScript types (foundation)

**`packages/preset-schema/src/`**
- `presetTypes.ts` — `HybridPresetV2`, `Layer` (sample/oscillator/noise/texture), `Effects`, `Macro`, `Quality`, `SourceImport`.
- `importTypes.ts` — `ImportCandidate`, `ImportResult`, `ImportReviewItem`, `RootNoteSource`.
- `categoryTemplates.ts` — Default templates for all 11 categories (DrillBells, AlienLeads, PainPianos, ChoirsVox, Guitars, DarkPads, Plucks, Bass808, FXRisers, Textures, Uncategorized).
- `presetValidators.ts` — Zod-style runtime validators + `isLegacyPreset()` / `migrateLegacyToV2()`.
- `index.ts` — barrel exports + `SCHEMA_VERSION = "2.0.0"`.

## Phase 2 — Python import pipeline rewrite

**`native/tools/import_samples.py`** (refactor, keep CLI):
- New layout: `Samples/Imported/<Category>/`, `Presets/User/<Category>/`, `Metadata/Imports/`, `Logs/`.
- New keyword classifier (DrillBells / AlienLeads / PainPianos / ChoirsVox / Guitars / DarkPads / Plucks / Bass808 / FXRisers / Textures / Uncategorized) with old-name fallback mapping.
- Stop skipping no-note files. Defaults: Bass808→C2, FXRisers/Textures→C4 + `pitchTracking:false`, others→C5. Set `quality.rootNoteVerified=false` and `needsReview=true` when guessed.
- Output per file: copied sample + `*.import.json` metadata + `*.didasynthpreset` (built from category template) + `Presets/index.json` upsert.
- Flags: `--dry-run`, `--inbox`, `--force`, `--review-json` (emits review payload without finalizing).

**`native/tools/migrate_old_samples.py`** — walks old `Samples/<Brass|Piano|...>/` layout and re-routes into the new `Imported/<NewCategory>/` + generates presets.

**`native/tools/validate_presets.py`** — JSON-schema validator for `.didasynthpreset` v2 files.

## Phase 3 — JUCE engine: layered Voice

**`native/plugin/Source/Sampler/`** (new)
- `SampleAsset.[h/cpp]`, `SampleMap.[h/cpp]`, `SampleLayer.[h/cpp]`, `RootNoteDetector.[h/cpp]`, `SampleImportMetadata.[h/cpp]`.
- `SampleLibrary` moves here (forwarding header left in DSP/ for compat).

**`native/plugin/Source/DSP/Voice.{h,cpp}`** — extend (don't replace):
- Hold up to 4 layer renderers (sample / oscillator / noise / texture).
- Per-layer: enabled, gain, pan, pitch+fine, filter, ADSR.
- Sum layers → global filter → existing `FxChain`.
- Restores oscillator/noise audibility for non-sample presets (also fixes the silent-preset bug for non-Brass factory presets).

## Phase 4 — Preset system v2 in C++

**`native/plugin/Source/Presets/`**
- `PresetSchema.h` — extend with v2 keys (`schemaVersion`, `layers[]`, `macros[]`, `globalFilter`, `effects.*`).
- `PresetManager.{h,cpp}` — load v2 presets directly; if `schemaVersion < 2` or absent, route through `PresetMigration::toV2()` then apply.
- `PresetMigration.{h,cpp}` — legacy `sampler.instrument` → Layer 1 sample preset; legacy oscillator params → Layer 2 oscillator.
- `PresetIndex.{h,cpp}` — read/write `Presets/index.json`, search by category/tag/needsReview.
- `PresetTemplateFactory.{h,cpp}` — C++ mirror of `categoryTemplates.ts` for in-plugin "New Preset from Template".
- `HybridPresetGenerator.{h,cpp}` — given a sample asset + category, build a v2 preset.
- `FactoryPresets.{h,cpp}` — keep existing factory list; tag schema v1, migrated on load.

## Phase 5 — Plugin UI: Simple / Advanced

**`native/plugin/Source/UI/`**
- `MainSynthPanel` — add Simple/Advanced toggle.
- Simple: preset browser, 8 macros, ADSR, cutoff/res, glide, reverb, delay, dist, width, master.
- Advanced (new): `LayerEditor` (4 tabs), `MacroPanel` (4 macros + mapping), `EffectsPanel` (chain), `ImportReviewPanel` (drop zone + review table → finalize).
- `PresetBrowser` — category sidebar driven by `PresetIndex`, search + Needs Review filter.

## Phase 6 — Web admin (`src/`)

- `pages/PresetsPage.tsx` — replace mock cards with: Library overview, Factory/User tabs, Import dropzone, search, category filters, Needs Review filter, preset grid, detail drawer.
- `components/admin/PresetImportPanel.tsx` — drop zone + review table (mirrors plugin review screen, talks to local file index via existing backend stubs).
- `components/admin/PresetBrowserAdmin.tsx` — table/grid powered by `index.json`.
- `components/admin/PresetTemplateEditor.tsx` — edit category templates JSON.

All visuals use existing semantic tokens in `index.css` / `tailwind.config.ts`.

## Phase 7 — Tests & docs

- `native/plugin/Tests/PresetValidationTests.cpp` — extend with v2 + migration cases.
- `native/tools/tests/` — pytest cases: bell→DrillBells, 808→Bass808/C2, FX→FXRisers+pitchTracking false, no-note doesn't skip, old folder migration, schema validates.
- `src/test/` — Vitest for `presetValidators`.
- Docs: `docs/PRESET_ARCHITECTURE.md`, `docs/IMPORT_PIPELINE.md`, `docs/HYBRID_LAYER_ENGINE.md`, plus update `DIDITAGAIN_README.md`.

---

## Backwards compatibility guarantees

- Existing `.didasynthpreset` files (incl. `Sampled_Brass`) continue to load via `PresetMigration`.
- Old `Samples/<Instrument>/` folders still resolve at runtime; `migrate_old_samples.py` upgrades them on demand.
- Existing CLI invocations of `import_samples.py <path>` keep working; new behaviors are additive.
- Public Voice API (setOscALevel, setFmAmount, etc.) preserved as either functional (Phase 3) or no-ops where the layer system supersedes them.

## Out of scope (call out explicitly)

- No new auth/billing changes.
- No DSP rewrite of existing oscillator/filter/FX modules — they're reused inside layers.
- No change to VST3 plugin ID / host automation parameter IDs (keeps existing FL projects loading).

## Suggested commit order

1. Phase 1 (schema + types) — pure additive, no risk.
2. Phase 2 (Python pipeline + migration script).
3. Phase 3 + 4 (engine layers + PresetManager v2 + migration) — single coherent C++ commit.
4. Phase 5 (plugin UI).
5. Phase 6 (web admin).
6. Phase 7 (tests + docs).

Approve this and I'll start with Phase 1 immediately. If you'd rather I narrow the first pass (e.g. just Phases 1–3 to unblock the plugin), say so and I'll cut scope.
