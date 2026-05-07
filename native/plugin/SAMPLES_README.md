# DIDITAGAIN STUDIO — Multisample Engine

The plugin is now a Nexus-style **multisample player**. Notes are produced
by pitch-shifting and crossfading real audio one-shots that you drop into
folders on disk.

## Where to put your samples

Create one folder per instrument under:

```
Documents/DIDITAGAIN STUDIO/Samples/<InstrumentName>/
```

The folder name **must exactly match** the `instrument` field of a preset
(case-sensitive on macOS/Linux). The plugin ships with factory presets for
the following folders — create any (or all) of them and drop one-shots in:

**Brass:** `Brass`, `Trumpet`, `Trombone`, `FrenchHorn`, `Saxophone`
**Strings:** `Strings`, `Violin`, `Cello`, `Pizzicato`, `Harp`
**Keys:** `Piano`, `GrandPiano`, `RhodesEP`, `WurliEP`, `Organ`, `Clavinet`, `Harpsichord`
**Mallet / tuned perc:** `Marimba`, `Vibraphone`, `Xylophone`, `Glockenspiel`, `MusicBox`, `Kalimba`
**Guitar:** `AcousticGuitar`, `ElectricGuitar`, `NylonGuitar`
**Bass:** `Bass`, `UprightBass`, `SynthBass`
**Wind:** `Flute`, `Clarinet`, `Oboe`, `PanFlute`
**Vocal:** `Choir`, `VocalAhh`, `VocalOoh`
**Drums:** `DrumKit`, `808Kit`, `Percussion`
**Synth / hybrid:** `SynthLead`, `SynthPad`, `SynthPluck`, `FX`
**World:** `Sitar`

You can also create your own folder name and reference it from a custom
preset's `"sampler": { "instrument": "MyFolder" }` field.

## Auto-importing samples (recommended)

Don't rename files by hand. Use the bundled importer:

```cmd
python native\tools\import_samples.py "C:\path\to\loose\samples"
```

It will:

1. **Detect the note** in each filename (e.g. `..._D#`, `Trumpet_F4`, `Rhodes A2`).
2. **Pick the right sub-folder** from keywords (`brass`, `trumpet`, `rhodes`,
   `808`, `glock`, `pad`, etc.) — these match the factory preset folders.
3. **Detect a velocity layer** from tags like `pp / mf / ff / soft / hard`
   or `_v90`.
4. **Copy + rename** the file into
   `Documents\DIDITAGAIN STUDIO\Samples\<Category>\<Category>_<Note>[_v<Vel>].wav`.

Useful flags:

- `--dry-run` &nbsp;preview the routing without copying
- `--inbox` &nbsp;&nbsp;&nbsp;process everything in `Documents\DIDITAGAIN STUDIO\Inbox`
- `--force` &nbsp;&nbsp;overwrite existing files

After the importer finishes, reload the preset (or restart FL) — the engine
re-scans the folder and immediately uses the new samples to build the sound.
## Filename convention

```
<anything>_<NoteName><Octave>[_v<Vel>].{wav|flac|ogg|mp3|aiff}
```

- **NoteName**: `C C# Db D D# Eb E F F# Gb G G# Ab A A# Bb B`
- **Octave**: `-1` … `9`  (C4 = MIDI 60, same as most DAWs)
- **Vel** (optional): `1`–`127`. The upper bound of a velocity layer.
  If omitted, the file covers the full velocity range.

### Examples

| Filename                | Root note | Velocity layer |
|-------------------------|-----------|----------------|
| `Brass_C3.wav`          | C3 (48)   | full range     |
| `Brass_F#3.wav`         | F#3 (54)  | full range     |
| `Brass_C4_v60.wav`      | C4 (60)   | 1–60 (soft)    |
| `Brass_C4_v100.wav`     | C4 (60)   | 61–100 (med)   |
| `Brass_C4_v127.wav`     | C4 (60)   | 101–127 (hard) |

You can sample as **few as one note per octave** — the engine pitch-shifts
and crossfades between the two nearest root notes for smooth coverage.
For Nexus-style realism, sample every 3–4 semitones with 2–3 velocity
layers per zone.

## How a preset chooses an instrument

A preset JSON now contains a `sampler` block:

```json
{
  "presetName": "Sampled Brass",
  "sampler": { "instrument": "Brass" },
  ...
}
```

The string must match a folder name under `Samples/`. If the folder is
missing or empty, the preset will be silent.

## Supported audio formats

WAV, FLAC, OGG/Vorbis, MP3, AIFF (anything JUCE's `registerBasicFormats`
supports). Stereo or mono input — mono files are mirrored to both channels.

## What still works from the synth UI

- Filter, filter envelope, filter key-tracking
- Amp envelope (attack / decay / sustain / release)
- Glide / portamento
- Velocity sensitivity
- Full FX chain (EQ, comp, chorus, delay, reverb, limiter, master gain)
- Mono / poly mode and voice count

The oscillator, FM, sub-osc, noise, unison, and pulse-width controls are
no longer audible — they're kept in the parameter list so old preset files
don't error, but the audio source is now your samples.
