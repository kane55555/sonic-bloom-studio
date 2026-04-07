# DIDITAGAIN STUDIO

A production-grade desktop audio software monorepo: VST3 synthesizer plugin, standalone app, web admin dashboard, and licensing system.

## Monorepo Structure

```
/native/plugin/          — C++17 JUCE VST3/Standalone plugin
  /Source/                — PluginProcessor, PluginEditor
  /Source/DSP/            — SynthEngine, Voice, Oscillator, Filter, Envelope, LFO, ModMatrix, FxChain
  /Source/UI/             — Theme, BrowserComponent
  /Source/Presets/         — PresetManager
  /Source/Licensing/       — LicenseClient, AccountState
  /Presets/Factory/        — 30 factory .didasynthpreset files
  CMakeLists.txt

/backend/                — Backend service stubs
  /db/schema.sql         — Supabase SQL schema with RLS
  /api/                  — License verification service
  /stripe/               — Stripe webhook handler

/packages/
  /preset-schema/        — TypeScript preset type definitions

/src/                    — Web admin dashboard (React/Vite — live in Lovable)
```

## Building the Native Plugin (Windows)

### Prerequisites
1. **Visual Studio 2022** with C++ desktop workload
2. **CMake 3.22+** — https://cmake.org/download/
3. **JUCE 7+** — https://juce.com/download/ (clone to `C:/JUCE`)
4. **VST3 SDK** — bundled with JUCE, or download from Steinberg

### Build Steps
```bash
cd native/plugin
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DJUCE_DIR=C:/JUCE
cmake --build . --config Release
```

### Output
- `build/DIDITAGAIN_STUDIO_artefacts/Release/VST3/DIDITAGAIN STUDIO.vst3`
- `build/DIDITAGAIN_STUDIO_artefacts/Release/Standalone/DIDITAGAIN STUDIO.exe`

### Install VST3 for FL Studio
Copy `.vst3` to: `C:\Program Files\Common Files\VST3\`
Then rescan plugins in FL Studio.

## Content Directory Layout (Windows)
```
C:\ProgramData\DIDITAGAIN\DIDITAGAIN STUDIO\
  Presets\Factory\        — Factory presets (installed)
  Presets\User\           — User-created presets
  Wavetables\             — Wavetable files
  Skins\                  — UI themes
  Logs\                   — Application logs

%APPDATA%\DIDITAGAIN\DIDITAGAIN STUDIO\
  license.cache           — Encrypted license token
  settings.json           — User preferences
```

## Web Admin Dashboard
The React admin dashboard runs in Lovable. Enable Lovable Cloud to activate:
- Supabase database (run `backend/db/schema.sql`)
- Stripe integration for subscriptions
- Auth for admin login

## Remaining Manual Tasks

### Must Do Before First Build
- [ ] Install JUCE 7+ and set `JUCE_DIR` path
- [ ] Install Visual Studio 2022 with C++ workload
- [ ] Run CMake build and fix any platform-specific issues
- [ ] Add a `JuceHeader.h` or configure JUCE module paths

### Must Do Before Release
- [ ] Enable Lovable Cloud and run `backend/db/schema.sql`
- [ ] Enable Stripe integration and implement webhook handler
- [ ] Implement JWT signing/verification with real keys
- [ ] Implement license cache encryption (AES-256)
- [ ] Implement hardware fingerprint using WMI (Windows)
- [ ] Build Windows installer (Inno Setup or WiX)
- [ ] Complete preset parameter serialization in PresetManager
- [ ] Add JUCE GUI controls (knobs, sliders, meters) to PluginEditor
- [ ] Implement FM synthesis mode in Voice/Oscillator
- [ ] Implement wavetable loading and rendering
- [ ] Add oversampling toggle
- [ ] Complete FX chain (phaser, compressor, EQ, limiter)
- [ ] Implement mod matrix processing in voice render loop
- [ ] Add MIDI learn support
- [ ] Code-sign the VST3 plugin and installer
- [ ] Test in FL Studio, Ableton, Bitwig
- [ ] Volume-balance all 30 factory presets
- [ ] Design final plugin GUI graphics

### Nice to Have
- [ ] macOS build support (Xcode project)
- [ ] Alternate tuning file support (.scl/.kbm)
- [ ] Wavetable editor
- [ ] Preset pack encryption for premium content
- [ ] User preset cloud sync
- [ ] In-plugin announcement feed
