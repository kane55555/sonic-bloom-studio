# Preset pack manifest

Each pack ships as a manifest JSON plus sample files. Schema and Zod
validator live in `packages/preset-schema/src/manifestTypes.ts`.

```jsonc
{
  "manifestVersion": "1.0.0",
  "packId": "5f1c…",
  "packSlug": "pain-pianos-vol-1",
  "packName": "Pain Pianos Vol. 1",
  "version": "1.2.0",
  "checksumSha256": "ab12…",
  "generatedAt": "2026-05-16T00:00:00Z",
  "presets": [
    {
      "name": "Felt Piano",
      "category": "Pianos",
      "instrumentType": "piano",
      "tags": ["soft", "intimate"],
      "rootKeyMode": "tracking",
      "sampleMappingMode": "full_multisample",
      "zones": [
        {
          "samplePath": "samples/felt_c2.wav",
          "rootNote": 36, "lowKey": 21, "highKey": 47,
          "velocityMin": 1, "velocityMax": 127,
          "loopStart": 22050, "loopEnd": 110250,
          "gainDb": 0, "tuningCents": 0,
          "tags": []
        }
      ]
    }
  ]
}
```

## Mapping modes

- `one_shot` — single zone across the keyboard, no pitch tracking.
- `auto_multisample` — plugin extrapolates roots across the keyboard from
  a small set of source samples.
- `full_multisample` — every zone is authored explicitly (recommended for
  factory packs).

## Storage layout

```
pack-manifests/<pack-slug>/<version>/manifest.json
pack-downloads/<pack-slug>/<version>/pack.zip
pack-covers/<pack-slug>.jpg
```

The first path segment (`<pack-slug>`) is what the entitled-read RLS
policy on `pack-manifests` matches against `preset_packs.slug`.
