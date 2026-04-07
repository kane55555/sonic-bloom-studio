
## DIDITAGAIN STUDIO — Full Monorepo Scaffold

### Phase 1: Project Structure & Build Config
- Create the full monorepo directory tree
- CMakeLists.txt for JUCE + VST3 build
- Package configs for shared TypeScript packages

### Phase 2: Core C++ Plugin Code (~20 files)
- PluginProcessor / PluginEditor
- SynthEngine, Voice, Oscillator, FilterBlock
- Envelope, LFO, ModMatrix, FxChain
- PresetManager, LicenseClient, AccountState
- Theme, BrowserComponent

### Phase 3: Preset System
- `.didasynthpreset` JSON schema (TypeScript + C++ compatible)
- 30 factory preset JSON files
- Preset validation utilities

### Phase 4: Web Admin Scaffold (React in Lovable)
- Admin dashboard landing page with navigation
- Users, Subscriptions, Presets, Activations, Announcements pages (stub UI)

### Phase 5: Backend Schema Stubs
- Supabase SQL migration file (ready for later)
- Stripe webhook handler stub
- License verification service stub

### Phase 6: Documentation
- README with Windows build steps (JUCE + CMake + VST3 SDK setup)
- Remaining manual tasks checklist

**Note:** C++ files will be scaffolded but require local JUCE/CMake/MSVC to compile. The web admin will be the live Lovable app.
