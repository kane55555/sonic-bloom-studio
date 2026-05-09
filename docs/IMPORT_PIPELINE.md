# Import Pipeline — DIDITAGAIN STUDIO v2

## Folder layout

```
Documents/DIDITAGAIN STUDIO/
  Inbox/                      # drop-zone for new audio
  Samples/
    Imported/<Category>/      # routed by category
    Factory/                  # bundled multisamples
    User/                     # user multisamples
  Presets/
    Factory/
    User/<Category>/*.didasynthpreset
    index.json                # searchable index (built by importer)
  Metadata/Imports/*.import.json
  Logs/
  Settings/
```

## CLI

```bash
python native/tools/import_samples.py <file_or_folder> [...]
python native/tools/import_samples.py --inbox
python native/tools/import_samples.py --dry-run <files>
python native/tools/import_samples.py --review-json review.json <files>
python native/tools/import_samples.py --from-review review.json
python native/tools/import_samples.py --force <files>
python native/tools/migrate_old_samples.py [--dry-run]
python native/tools/validate_presets.py [path]
```

## Pipeline steps

1. **Classify** the file via filename keywords (`CATEGORY_KEYWORDS`),
   then legacy folder name fallback, then `Uncategorized`.
2. **Detect root note** from the filename. If absent:
   - `Bass808` → C2
   - `FXRisers`, `Textures` → C4 + `pitchTracking: false`
   - everything else → C5
   - `quality.rootNoteVerified = false`, `needsReview = true`.
3. **Detect velocity** via `_v90` / `pp` / `mf` / `stab` / `sustain` etc.
4. **Copy** the sample into `Samples/Imported/<Category>/`.
5. **Generate** a hybrid preset from the category template, stamping in
   the sample path + root note as Layer 1.
6. **Write** an `import.json` metadata file.
7. **Upsert** an entry into `Presets/index.json`.

## Review workflow

`--review-json out.json <files>` writes a JSON list of `ImportCandidate`
objects (filename, category, root note, target paths, warnings, …) without
copying anything. The web admin or plugin UI displays these for review;
the user edits category / root note / template / pitch-tracking, then the
edited list is fed back via `--from-review edited.json` to finalize.
