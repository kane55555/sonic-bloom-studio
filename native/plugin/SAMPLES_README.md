# DIDITAGAIN STUDIO — Multisample Engine

The plugin is now a Nexus-style **multisample player**. Notes are produced
by pitch-shifting and crossfading real audio one-shots that you drop into
folders on disk.

## Where to put your samples

Create one folder per instrument under:

```
Documents/DIDITAGAIN STUDIO/Samples/<InstrumentName>/
```

For example:

```
Documents/DIDITAGAIN STUDIO/Samples/
├── Brass/
│   ├── Brass_C3.wav
│   ├── Brass_F#3.wav
│   ├── Brass_C4.wav
│   └── Brass_F#4_v90.wav
├── Strings/
│   └── ...
```

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
