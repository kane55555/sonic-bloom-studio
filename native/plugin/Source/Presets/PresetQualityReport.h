#pragma once
//==============================================================================
//  PresetQualityReport.h — DEBUG-ONLY preset quality reporter.
//
//  Emits one structured "[DIDITAGAIN preset-quality]" block to the JUCE log
//  every time a .diapreset finishes loading. This is purely diagnostic and
//  does NOT alter any sound or DSP behaviour.
//
//  Fields cover preset identity, layer/partial counts, FX mix levels, peak
//  meters captured from LayerBusProcessor + FxChain, and a short list of
//  warning labels (TOO_HOT, TOO_WET, NO_ZONES, ...).
//==============================================================================
#include <JuceHeader.h>
#include "UserPresetFormat.h"
#include "../PluginProcessor.h"
#include "../DSP/SynthEngine.h"

namespace dida { namespace presetreport {

struct CapsForCategory { float chorus, delay, reverb, sat; };

// --- Session-scoped state (one plugin instance / process lifetime) --------
struct SessionState
{
    juce::String sessionId;
    int          loadIndex = 0;
    juce::String lastPresetKey;
    juce::int64  lastLoadMs = 0;
};

inline SessionState& sessionState()
{
    static SessionState s;
    if (s.sessionId.isEmpty())
        s.sessionId = juce::Uuid().toDashedString();
    return s;
}

inline juce::File logsDir()
{
   #if JUCE_WINDOWS
    juce::File win ("C:/Users/kaini/Documents/DIDITAGAIN STUDIO/Logs");
    if (win.getParentDirectory().isDirectory())
    {
        if (! win.isDirectory()) win.createDirectory();
        return win;
    }
   #endif
    auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                 .getChildFile("DIDITAGAIN STUDIO").getChildFile("Logs");
    if (! d.isDirectory()) d.createDirectory();
    return d;
}

inline juce::File sessionJsonlFile() { return logsDir().getChildFile("preset_quality_session.jsonl"); }
inline juce::File sessionTextFile()  { return logsDir().getChildFile("preset_quality_session.txt"); }
inline juce::File latestJsonFile()   { return logsDir().getChildFile("latest_preset_quality.json"); }

inline void ensureSessionFiles()
{
    auto jl = sessionJsonlFile();
    auto tx = sessionTextFile();
    if (! jl.existsAsFile()) jl.create();
    if (! tx.existsAsFile()) tx.create();
}

inline void clearSessionLog()
{
    sessionJsonlFile().replaceWithText({});
    sessionTextFile().replaceWithText({});
}

// Call once on plugin startup. Honors clearPresetLogOnStartup user prop.
inline void initSession(bool clearOnStartup = false)
{
    (void) sessionState();
    ensureSessionFiles();
    if (clearOnStartup) clearSessionLog();
}

// Normalise a category string for caps lookup: collapse typos
// (Saxaphone -> Saxophone), camelCase aliases (VintageSynth -> Vintage Synths),
// and stylistic prefixes (Trap Saxophone -> Saxophone).
inline juce::String normalizeCategory(const juce::String& in)
{
    auto s = in.trim();
    if (s.isEmpty()) return s;
    s = s.replace("Saxaphone", "Saxophone", true);
    s = s.replace("saxaphone", "saxophone", true);
    if (s.equalsIgnoreCase("VintageSynth") || s.equalsIgnoreCase("VintageSynths"))
        s = "Vintage Synths";
    for (auto* pfx : { "Trap ", "Lo-Fi ", "Lofi ", "Vintage " })
    {
        const juce::String p (pfx);
        if (s.startsWithIgnoreCase(p)) { s = s.substring(p.length()).trim(); break; }
    }
    return s;
}

inline CapsForCategory capsForCategory(const juce::String& catIn)
{
    const auto c = normalizeCategory(catIn).toLowerCase();
    // Aligned with UserPresetLoader::fxLimitsFor.
    if (c.contains("piano") || c.contains("keys") || c.contains("rhodes"))
        return { 0.18f, 0.14f, 0.34f, 0.12f };
    if (c.contains("lead"))             return { 0.40f, 0.45f, 0.35f, 0.40f };
    if (c.contains("pad"))              return { 0.42f, 0.34f, 0.50f, 0.25f };
    if (c.contains("choir") || c.contains("vox") || c.contains("vocal"))
                                        return { 0.32f, 0.26f, 0.46f, 0.18f };
    if (c.contains("brass") || c.contains("trumpet") || c.contains("horn") || c.contains("sax"))
                                        return { 0.20f, 0.12f, 0.28f, 0.28f };
    if (c.contains("guitar"))           return { 0.34f, 0.35f, 0.42f, 0.24f };
    if (c.contains("bell"))             return { 0.30f, 0.30f, 0.46f, 0.18f };
    if (c.contains("pluck"))            return { 0.30f, 0.32f, 0.38f, 0.18f };
    if (c.contains("808") || c.contains("bass") || c.contains("sub"))
                                        return { 0.10f, 0.14f, 0.16f, 0.32f };
    if (c.contains("riser") || c.contains("fx"))
                                        return { 0.50f, 0.55f, 0.48f, 0.50f };
    if (c.contains("synth"))            return { 0.18f, 0.10f, 0.18f, 0.16f };
    return { 0.45f, 0.40f, 0.45f, 0.38f };
}

// Per-category target peak window (dBFS) for active notes/chords.
struct LoudnessTarget { float minDb, maxDb; };

inline LoudnessTarget loudnessTargetForCategory(const juce::String& catIn)
{
    const auto c = normalizeCategory(catIn).toLowerCase();
    if (c.contains("piano") || c.contains("rhodes") || c.contains("keys"))
        return { -18.0f, -10.0f };
    if (c.contains("brass") || c.contains("trumpet") || c.contains("horn")
        || c.contains("sax")  || c.contains("guitar"))
        return { -16.0f,  -8.0f };
    if (c.contains("choir") || c.contains("vox") || c.contains("vocal"))
        return { -18.0f, -11.0f };
    if (c.contains("string") || c.contains("pad"))
        return { -20.0f, -10.0f };
    if (c.contains("808") || c.contains("bass") || c.contains("sub"))
        return { -12.0f,  -6.0f };
    if (c.contains("lead") || c.contains("vintage") || c.contains("synth"))
        return { -14.0f,  -6.0f };
    return { -18.0f, -8.0f };
}

// engineType-aware: pure synth engines do NOT need sourceInstrument.path.
// Layered engines need a source only if any enabled partial is "pcm".
inline bool engineRequiresSource(const dida::userpreset::UserPreset& up)
{
    auto needsPcm = [](const juce::String& e) {
        const auto x = e.trim().toLowerCase();
        return x.isEmpty() || x == "pcm" || x == "sample" || x == "multisample";
    };
    if (up.partials.size() == 0) return needsPcm(up.engineType);
    if (up.engineType.equalsIgnoreCase("layered") || up.engineType.isEmpty())
    {
        for (auto& p : up.partials)
            if (p.enabled && needsPcm(p.engineType)) return true;
        return false;
    }
    return needsPcm(up.engineType);
}

inline float paramValue(juce::AudioProcessor& proc, const char* id)
{
    for (auto* p : proc.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            if (withId->paramID == id)
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(withId))
                    return r->convertFrom0to1(r->getValue());
    return 0.0f;
}

inline juce::String fmt(float v, int digits = 3)
{
    return juce::String(v, digits);
}

inline juce::String dbStr(float linear)
{
    if (linear <= 1.0e-6f) return "-inf";
    return juce::String(juce::Decibels::gainToDecibels(linear), 2);
}

inline void report(DiditagainProcessor& proc,
                   const dida::userpreset::UserPreset& up,
                   const juce::String& effectiveCategoryIn,
                   const juce::String& resolvedFolderPath,
                   int wavZones,
                   const juce::String& sourceInstrumentPathRaw = {},
                   const juce::String& resolvedFromIn          = {},
                   bool rawPathInsidePresetsUser               = false,
                   const juce::String& browserPresetNameIn     = {},
                   const juce::String& bankCategoryIn          = {},
                   const juce::String& presetFilePathIn        = {},
                   const juce::String& presetCategoryFolderIn  = {},
                   const juce::String& expectedSourceFolderNameIn = {},
                   bool allowCrossCategorySourceIn             = false,
                   int sourceFolderWavCountIn                  = 0,
                   const juce::StringArray& extraSourceWarningsIn = {})
{
    auto& engine = proc.getSynthEngine();
    auto& bus    = engine.getLayerBus();
    auto& fx     = engine.getFx();

    const juce::String effectiveCategory = normalizeCategory(effectiveCategoryIn);
    const auto cLow = effectiveCategory.toLowerCase();
    const auto nLow = up.presetName.toLowerCase();
    const bool choirMode = cLow.contains("choir") || cLow.contains("vox")
                         || cLow.contains("vocal") || nLow.contains("choir") || up.choirMode;

    // --- Layer / partial counts -------------------------------------------
    const bool naturalChoirSampleOnly = choirMode;
    int activeLayers = (up.main.enabled ? 1 : 0)
                     + ((up.layer2.enabled && ! naturalChoirSampleOnly) ? 1 : 0);
    int activePartials = 0;
    if (! naturalChoirSampleOnly)
        for (auto& p : up.partials) if (p.enabled) ++activePartials;

    const juce::String engineType = up.engineType.isNotEmpty() ? up.engineType
                                  : (up.partials.size() > 0 ? juce::String("layered") : juce::String("pcm"));

    const bool needsSource = engineRequiresSource(up);
    const bool oscillatorEngineActive = ! needsSource;

    // --- FX mixes ---------------------------------------------------------
    const float chorusMix = paramValue(proc, "fxChorusMix");
    const float delayMixParam  = paramValue(proc, "fxDelayMix");
    const float reverbMixParam = paramValue(proc, "fxReverbMix");
    const float reverbSizeParam = paramValue(proc, "fxReverbSize");
    const float satMix    = paramValue(proc, "fxSaturationMix");
    const int   polyphony = (int) paramValue(proc, "polyphony");

    // --- Live FX-chain state for scale-safety report ----------------------
    auto& reverbBlk = fx.getReverb();
    auto& delayBlk  = fx.getDelay();
    const float delayMix  = choirMode ? delayBlk.getMix() : delayMixParam;
    const float reverbMix = choirMode ? reverbBlk.getMix() : reverbMixParam;
    const float reverbSize = choirMode ? reverbBlk.getSize() : reverbSizeParam;
    const float reverbDuckAmount   = reverbBlk.getDuckAmount();
    const bool  reverbDuckEnabled  = reverbDuckAmount > 0.0001f;
    const float reverbInputHpHz    = reverbBlk.getInputHighPassHz();
    const float reverbInputLpHz    = reverbBlk.getInputLowPassHz();
    const float delayDuckAmount    = delayBlk.getDuckAmount();
    const bool  delayDuckEnabled   = delayDuckAmount > 0.0001f;
    const float delayFeedback      = delayBlk.getFeedback();
    const bool  scaleSafeFxMode    = fx.getScaleSafeFxMode();
    const bool  noteDensityFxOn    = fx.getNoteDensityFxReductionEnabled();
    const bool  fxTailClearOnLoad  = fx.getClearFxTailOnPresetChange();

    // --- Peak snapshots (last logged-frame peak; 0 = silent so far) -------
    const float busPeakDb   = bus.getRecentPeakDb();
    const float fxInDb      = fx.getFxInPeakDb();
    const float fxOutDb     = fx.getFxOutPeakDb();
    const float finalDb     = fx.getFinalPeakDb();
    const float headroomDb  = juce::jmax(-60.0f, -finalDb);

    // Task 6/7: dedicated dry-bus / isolated wet-return / final-output meters.
    const float dryOutputDb    = fx.getDryOutputPeakDb();
    const float finalOutputDb  = fx.getFinalOutputPeakDb();
    // BUG 2: the report runs at preset-load time with NO fresh audition render,
    // so the live return meters can hold a stale tail from the previous preset.
    // When the engine has latched reverb/delay silent (hard-bypass or the
    // resolved mix is 0), the return is DEFINITIONALLY silent — report -120
    // (linear 0) instead of trusting a possibly-stale meter.
    const bool reverbLatchedSilent = fx.getReverbHardBypass();
    const bool delayLatchedSilent  = fx.getDelayHardBypass();
    const float reverbReturnDb = reverbLatchedSilent ? -120.0f : fx.getReverbReturnPeakDb();
    const float delayReturnDb  = delayLatchedSilent  ? -120.0f : fx.getDelayReturnPeakDb();


    // resolvedFrom — explicit caller value wins; otherwise infer from state.
    juce::String resolvedFrom = resolvedFromIn;
    if (resolvedFrom.isEmpty())
    {
        if (! needsSource)                        resolvedFrom = "notRequiredForEngine";
        else if (resolvedFolderPath.isNotEmpty()) resolvedFrom = "fallbackSearch";
        else                                       resolvedFrom = "unresolved";
    }

    // --- Warnings ---------------------------------------------------------
    juce::StringArray warnings;
    if (finalDb > -1.0f)                            warnings.add("TOO_HOT");
    const auto caps = capsForCategory(effectiveCategory);
    if (reverbMix > caps.reverb + 0.001f || delayMix > caps.delay + 0.001f
        || chorusMix > caps.chorus + 0.001f || satMix > caps.sat + 0.001f)
        warnings.add("TOO_WET");
    if (up.layer2.enabled && up.layer2.gainDb > 0.0f) warnings.add("LAYER_TOO_LOUD");

    const auto resolved = juce::File(resolvedFolderPath);
    const bool wantsFolder = up.source.type.isEmpty()
                          || up.source.type.equalsIgnoreCase("multisampleFolder");

    // SOURCE_MISSING is only a problem when the active engine actually needs
    // a sample folder. Pure synth presets (analog/supersaw/fm*/wavetable)
    // legitimately have no sourceInstrument.
    if (needsSource && wantsFolder && up.source.path.isNotEmpty()
        && (resolvedFolderPath.isEmpty() || ! resolved.isDirectory()))
        warnings.add("SOURCE_MISSING");
    if (needsSource && wantsFolder && resolved.isDirectory() && wavZones == 0)
        warnings.add("NO_ZONES");

    // Source folders are allowed to live inside the preset's own category
    // under Samples/Presets/User/<Category>/ as hidden siblings of the
    // .diapreset files. Only warn when the resolved folder is inside a
    // DIFFERENT preset category, or when the raw path explicitly points
    // into a foreign Presets/User location.
    const auto rawNorm      = sourceInstrumentPathRaw.replaceCharacter('\\', '/');
    const auto resolvedNorm = resolvedFolderPath.replaceCharacter('\\', '/');
    const bool hiddenSourceFolder    = (resolvedFromIn == "categoryHiddenSourceFolder")
                                    || resolvedNorm.containsIgnoreCase("/Samples/Presets/User/"
                                                                       + effectiveCategory + "/");
    const bool resolvedInPresetsUser = resolvedNorm.containsIgnoreCase("/Samples/Presets/User/");
    const bool foreignPresetsUser    = resolvedInPresetsUser && ! hiddenSourceFolder;
    juce::ignoreUnused(rawNorm, rawPathInsidePresetsUser);
    if (foreignPresetsUser)
        warnings.add("SOURCE_PATH_INSIDE_PRESET_FOLDER");

    for (auto& w : extraSourceWarningsIn)
    {
        // Never propagate SOURCE_MISSING when the active engine does not
        // require a sample folder (pure synth presets: analog, supersaw,
        // fm2, fm4, wavetable, ...).
        if (! needsSource && w == "SOURCE_MISSING") continue;
        if (! warnings.contains(w)) warnings.add(w);
    }

    const bool lowEndCat = cLow.contains("808") || cLow.contains("bass") || cLow.contains("sub");
    if (lowEndCat && (reverbMix > 0.10f || delayMix > 0.12f))
        warnings.add("TOO_MUCH_LOW_END");

    // --- FX scale-safety warnings -----------------------------------------
    // Big reverb on melodic/scale-friendly categories blurs note transitions.
    const bool scaleSensitive = cLow.contains("piano")  || cLow.contains("rhodes")
                             || cLow.contains("guitar") || cLow.contains("brass")
                             || cLow.contains("sax")    || cLow.contains("trumpet")
                             || cLow.contains("horn")   || cLow.contains("bell")
                             || cLow.contains("pluck")  || cLow.contains("lead");
    if (scaleSensitive && reverbSize > 0.65f)
        warnings.add("REVERB_TOO_LONG_FOR_SCALE");
    if (scaleSensitive && reverbMix > 0.30f && ! reverbDuckEnabled)
        warnings.add("FX_TAIL_BUILDUP_RISK");
    if (delayFeedback > 0.55f)
        warnings.add("DELAY_FEEDBACK_TOO_HIGH");
    if (reverbInputHpHz < 120.0f && reverbMix > 0.15f)
        warnings.add("REVERB_LOW_MID_BUILDUP");

    // --- Choir-mode safety state + warnings -------------------------------
    const float fxSendReleaseMsLive = proc.getSynthEngine().getFxSendReleaseMs();
    // BUG 1: choir caps may ONLY lower a release that exceeds the safety max —
    // they must never raise a short preset release (e.g. 1 ms) up to a floor.
    // The reported choir value mirrors the real applied engine value, bounded
    // only by an upper cap, so it equals appliedFxSendReleaseMs in every case.
    constexpr float kChoirFxSendReleaseMaxMs = 180.0f;
    const float choirFxSendReleaseMs = choirMode
        ? juce::jmin(kChoirFxSendReleaseMaxMs, fxSendReleaseMsLive)
        : fxSendReleaseMsLive;
    // Source reflects the true origin: a preset-supplied value is presetFxSend,
    // never choirModeClamp. choirModeClamp only applies when the choir cap
    // actually had to reduce the live value.
    const juce::String fxSendReleaseSource =
          up.fxSend.hasFxSendReleaseMs                                      ? juce::String("presetFxSend")
        : (choirMode && fxSendReleaseMsLive > kChoirFxSendReleaseMaxMs)     ? juce::String("choirModeClamp")
        : up.fxSend.hasFxSendReleaseMultiplier                             ? juce::String("ampReleaseFallback")
        : juce::String("categoryDefault");
    const bool  choirAmpReleaseClamped = choirMode && (up.amp.releaseMs > 900.0f);

    // BUG 5: choir caps may only LOWER dangerous values. Report a cap as applied
    // ONLY when a real before/after change occurred, and list the exact field.
    juce::StringArray choirCapFieldsApplied;
    if (choirMode)
    {
        auto addCap = [&choirCapFieldsApplied](const char* name, float before, float after,
                                               bool loweredIsCap, int digits)
        {
            const float eps = digits == 0 ? 0.5f : 0.001f;
            const bool changed = loweredIsCap ? (after < before - eps) : (after > before + eps);
            if (changed)
                choirCapFieldsApplied.add(juce::String(name) + " "
                    + juce::String(before, digits) + "→" + juce::String(after, digits));
        };
        const float reqReverbMix  = up.reverb.enabled && ! up.reverb.bypass ? up.reverb.mix : 0.0f;
        addCap("reverbMix",  reqReverbMix, reverbMix, true, 3);
        addCap("reverbSize", up.reverb.size, reverbSize, true, 3);
        if (up.reverb.inputHighpassHz >= 0.0f) addCap("reverbInputHighpassHz", up.reverb.inputHighpassHz, reverbInputHpHz, false, 0);
        if (up.reverb.inputLowpassHz  >= 0.0f) addCap("reverbInputLowpassHz",  up.reverb.inputLowpassHz,  reverbInputLpHz,  true,  0);
        const float reqDelayMix = up.delay.enabled ? up.delay.mix : 0.0f;
        const float reqDelayFb  = up.delay.enabled ? up.delay.feedback : 0.0f;
        addCap("delayMix",      reqDelayMix, delayMix, true, 3);
        addCap("delayFeedback", reqDelayFb,  delayFeedback, true, 3);
    }
    const bool choirReverbCapApplied = choirCapFieldsApplied.joinIntoString(",").containsIgnoreCase("reverb");
    const bool choirDelayCapApplied  = choirCapFieldsApplied.joinIntoString(",").containsIgnoreCase("delay");
    const int   choirActiveVoiceCount  = choirMode ? proc.getSynthEngine().getActiveVoiceCount() : 0;
    const bool  choirNoteDensityFxReduction = choirMode && noteDensityFxOn;


    // --- Preset JSON vs applied engine state (Tasks 6/7/8) ----------------
    // These expose what the preset asked for next to what the engine actually
    // applied, and drive the PRESET_VALUE_NOT_APPLIED mismatch warnings.
    const bool  presetReverbSilenced       = (! up.reverb.enabled) || up.reverb.bypass;
    const float presetJsonReverbMix        = presetReverbSilenced ? 0.0f : up.reverb.mix;
    // appliedReverb/DelayMix now read directly from the live FX-chain DSP block
    // (not the APVTS parameter), so the report reflects what the engine is
    // actually rendering after hard-bypass latches and choir caps are applied.
    const float appliedReverbMix           = reverbBlk.getMix();
    const float presetJsonDelayMix         = up.delay.enabled ? up.delay.mix : 0.0f;
    const float appliedDelayMix            = delayBlk.getMix();
    const float presetJsonDelayFeedback    = up.delay.enabled ? up.delay.feedback : 0.0f;
    const float appliedDelayFeedback       = delayBlk.getFeedback();
    const bool  presetHasFxSendReleaseMs   = up.fxSend.hasFxSendReleaseMs;
    const float presetJsonFxSendReleaseMs  = up.fxSend.fxSendReleaseMs;
    // For choir presets the clamp output is the legitimate target; otherwise the
    // live engine value should match the preset's request.
    const float appliedFxSendReleaseMs     = fxSendReleaseMsLive;
    const float expectedFxSendReleaseMs    = choirMode ? choirFxSendReleaseMs
                                                       : presetJsonFxSendReleaseMs;

    // -- Mismatch detection --
    const bool reverbReturnNotSilent = reverbReturnDb > -90.0f;
    const bool delayReturnNotSilent  = delayReturnDb  > -90.0f;
    const bool fxSendReleaseMismatch = presetHasFxSendReleaseMs
        && std::abs(appliedFxSendReleaseMs - expectedFxSendReleaseMs)
             > juce::jmax(1.0f, expectedFxSendReleaseMs * 0.15f);

    // Per-field list of exactly which preset JSON values did not reach the
    // engine. A category cap legitimately LOWERS a value, so we only flag a
    // field when the applied value EXCEEDS what the preset asked for (the
    // "stuck at old default / category override" failure mode), or when a
    // silenced effect is still audible at its return.
    juce::StringArray presetValueMismatchFields;
    auto appliedExceeds = [](float applied, float jsonVal) {
        return applied > jsonVal + juce::jmax(0.01f, std::abs(jsonVal) * 0.15f);
    };

    bool presetValueNotApplied = false;
    // When the preset asks for zero reverb AND the engine applied zero, there is
    // no mismatch — never flag reverbMix in that case (Report 71).
    const bool reverbBothZero = presetJsonReverbMix <= 0.0011f && appliedReverbMix <= 0.0011f;
    if (presetReverbSilenced && reverbReturnNotSilent)
    {
        // BUG 2: a leaking reverb tail is an engine routing fault, NOT a preset
        // value mismatch. Only flag the leak — do not add reverbMix to the
        // mismatch list and do not raise PRESET_VALUE_NOT_APPLIED when the
        // preset and applied reverb mix are both zero.
        warnings.add("REVERB_BYPASS_NOT_SILENT");
    }
    else if (! presetReverbSilenced && ! reverbBothZero
             && appliedExceeds(appliedReverbMix, presetJsonReverbMix))
    {
        presetValueMismatchFields.addIfNotAlreadyThere("reverbMix");
        presetValueNotApplied = true;
    }
    if (! up.delay.enabled && delayReturnNotSilent)
    {
        warnings.add("DELAY_OFF_NOT_SILENT");
        presetValueMismatchFields.addIfNotAlreadyThere("delayMix");
        presetValueNotApplied = true;
    }
    else if (up.delay.enabled)
    {
        if (appliedExceeds(appliedDelayMix, presetJsonDelayMix))
        {
            presetValueMismatchFields.addIfNotAlreadyThere("delayMix");
            presetValueNotApplied = true;
        }
        if (appliedExceeds(appliedDelayFeedback, presetJsonDelayFeedback))
        {
            presetValueMismatchFields.addIfNotAlreadyThere("delayFeedback");
            presetValueNotApplied = true;
        }
    }
    if (fxSendReleaseMismatch)
    {
        warnings.add("FX_SEND_RELEASE_NOT_APPLIED");
        presetValueMismatchFields.addIfNotAlreadyThere("fxSendReleaseMs");
        presetValueNotApplied = true;
    }
    if (presetValueNotApplied)
        warnings.add("PRESET_VALUE_NOT_APPLIED");

    // Task 9/10 (Report 71) + BUG 3 (Report 72): silence diagnosis with an
    // EXACT root cause along the real dry signal chain:
    //     voice render -> layerBus glue -> master gain (== amp.gainDb)
    // Each stage now has its own meter probe so a silent/collapsed dry bus is
    // attributed to the precise stage that ate the signal.
    const float voicePreLayerDb = fx.getDryVoicePreLayerPeakDb(); // raw voice, pre-layerBus
    const float postLayerDb     = fx.getDryVoiceRawPeakDb();      // post-layer, pre-master
    const float dryRawPeakDb    = postLayerDb;                    // (alias kept for fields below)
    const float masterGainDb    = fx.getMasterGainDb();           // applied master TRIM (±6)
    // Report 78 gain-staging: amp.gainDb is now its OWN stage (ampGain) applied
    // upstream of the dry meters, so dryRawPeakDb already reflects preset
    // loudness and master is only a small final trim. amp and master no longer
    // share a stage and cannot fight each other.
    const float presetJsonAmpGainDb    = up.amp.gainDb;                     // requested by preset JSON
    const float appliedAmpGainDb       = fx.getAmpGainDb();                 // applied amp-gain stage
    const float presetJsonMasterGainDb = fx.getMasterGainRequestedDb();    // requested master (pre-clamp)
    const float appliedMasterGainDb    = masterGainDb;                      // applied master (post-clamp)
    const float autoNormalizeGainDb    = 0.0f;                             // no auto-normalization: report observes only
    const float limiterGainReductionDb = fx.getLimiterGainReductionDb();
    const bool  intentionalMute        = false;                            // no preset mute field yet
    const juce::String masterGainSource = fx.wasMasterGainClamped() ? juce::String("safetyClamp")
                                        : (std::abs(appliedMasterGainDb) < 0.01f ? juce::String("default")
                                                                                 : juce::String("preset"));
    // CRITICAL CHECK 1 (Report 79): amp-gain truthfulness. appliedAmpGainDb is the
    // LIVE amp stage read back from the engine; requestedAmpGainDb is the raw JSON
    // request. When they differ the source must NOT claim "preset" — it must name
    // the exact reason (safety clamp to [-60,+24] or a load-time gain trim such as
    // the choir-mode trim) with before/after values exposed via requestedAmpGainDb
    // / appliedAmpGainDb / ampGainClampReason.
    const float requestedAmpGainDb = presetJsonAmpGainDb;
    const bool  ampGainDiffers     = std::abs(appliedAmpGainDb - requestedAmpGainDb) > 0.1f;
    juce::String ampGainClampReason = "none";
    if (ampGainDiffers)
    {
        if (requestedAmpGainDb > 24.0f && appliedAmpGainDb >= 23.9f)
            ampGainClampReason = "safetyClampMax(+24dB)";
        else if (requestedAmpGainDb < -60.0f && appliedAmpGainDb <= -59.9f)
            ampGainClampReason = "safetyClampMin(-60dB)";
        else
            ampGainClampReason = "loadTimeGainTrim"; // additive trim applied at load (e.g. choir)
    }
    const juce::String ampGainSource =
          ampGainDiffers ? (ampGainClampReason.startsWith("safetyClamp") ? juce::String("safetyClamp")
                                                                         : juce::String("presetTrim"))
        : (std::abs(requestedAmpGainDb) < 0.01f ? juce::String("default") : juce::String("preset"));
    const float ampGainDb           = appliedAmpGainDb;        // alias now reflects the REAL applied amp gain
    const float mainLayerGainDb     = up.main.gainDb;
    const float layer2GainStageDb   = up.layer2.enabled ? up.layer2.gainDb : -120.0f;
    const float masterMinusAmpDb    = masterGainDb - ampGainDb;
    const bool  masterGainMatchesAmp = std::abs(masterMinusAmpDb) <= 0.5f;
    const float voicePreAmpDb       = fx.getDryVoicePreAmpPeakDb();
    // Gain-stage accounting: dryOutput is metered as dryRaw * masterTrim, so the
    // expected value is exactly dryRaw + masterTrim. A divergence > 3 dB means a
    // metering/gain-stage bug rather than intentional gain.
    const float dryOutputExpectedDb = dryRawPeakDb + masterGainDb;
    // CRITICAL CHECK 3 (Report 79): voice -> dry gain accounting. The dry bus is
    // built as: voicePreAmp --(+appliedAmpGain)--> layerBus(unity glue) --(+masterTrim)--> dryOutput.
    // EXPECTED end-to-end voice->dry gain is therefore appliedAmpGainDb + masterGainDb.
    // ACTUAL is dryOutput - voicePreAmp. A mismatch beyond a few dB that is NOT
    // attributable to the limiter (metered separately as limiterGainReductionDb)
    // points at a stage moving gain silently. Reported as numbers only — the hard
    // GAIN_STAGE_ACCOUNTING_MISMATCH warning stays on the exact master path so the
    // glue stage's legitimate peak shaping never trips a false positive.
    const float expectedVoiceToDryGainDb = appliedAmpGainDb + masterGainDb;
    const bool  voiceDryMeasurable       = voicePreAmpDb > -100.0f && dryOutputDb > -100.0f;
    const float actualVoiceToDryGainDb   = voiceDryMeasurable ? (dryOutputDb - voicePreAmpDb) : 0.0f;
    const float voiceToDryGainMismatchDb = voiceDryMeasurable ? (actualVoiceToDryGainDb - expectedVoiceToDryGainDb) : 0.0f;
    // MASTER_GAIN_CLAMPED / UNINTENTIONAL_MASTER_MUTE surface presets that tried
    // to use master gain as a big loudness correction (now refused).
    if (fx.wasMasterGainClamped())
        warnings.add("MASTER_GAIN_CLAMPED");
    if (presetJsonMasterGainDb <= -40.0f && ! intentionalMute)
        warnings.add("UNINTENTIONAL_MASTER_MUTE");
    // GAIN_STAGE_ACCOUNTING_MISMATCH: dryOutput must equal dryRaw + masterTrim.
    if (dryRawPeakDb > -100.0f && dryOutputDb > -100.0f
        && std::abs(dryOutputDb - dryOutputExpectedDb) > 3.0f)
        warnings.add("GAIN_STAGE_ACCOUNTING_MISMATCH");
    // BUG 4: probe a test note that always lands on a real zone (covering zone
    // for C4, else nearest zone root) so a root-only sample map never reports a
    // false NO_ZONE_FOR_TEST_NOTE.
    const auto  zoneProbe       = engine.probeReportZone(60, 100);
    const int   silenceTestNote = zoneProbe.testMidiNote >= 0 ? zoneProbe.testMidiNote : 60;
    const int   zoneCount       = engine.getActiveZoneCount();
    const int   zoneCoverage    = zoneCount == 0 ? 0 : (zoneProbe.usedNearest ? 2 : 1);
    // BUG 4/5: surface the EXACT zone the probe landed on so a "no zone" report
    // is always backed by the real first/last/selected roots and the file used.
    const int   firstZoneRoot         = zoneProbe.firstZoneRoot;
    const int   lastZoneRoot          = zoneProbe.lastZoneRoot;
    const int   selectedZoneRoot      = zoneProbe.selectedZoneRoot;
    const juce::String selectedZoneFile = zoneProbe.selectedZoneFile;
    const bool  zoneFallbackUsed      = zoneProbe.usedNearest;
    const int   zoneDistanceSemitones = (zoneProbe.hasZone && zoneProbe.selectedZoneRoot >= 0)
                                        ? std::abs(zoneProbe.selectedZoneRoot - silenceTestNote)
                                        : -1;
    // CRITICAL CHECK 6 (Report 79): voice/zone start proxies for DRY_BUS_SILENT
    // diagnosis. Derived from the zone probe + preset layer state (no extra RT
    // hooks): a voice can start when a real zone backs the test note AND the main
    // layer is enabled; the sample reader is considered started when that zone
    // resolved to a concrete file.
    const bool  mainLayerEnabled    = up.main.enabled;
    const bool  voiceStarted        = zoneProbe.hasZone && mainLayerEnabled;
    const bool  sampleReaderStarted = zoneProbe.hasZone && selectedZoneFile.isNotEmpty();
    juce::String drySilenceReason;
    if (dryOutputDb <= -60.0f && up.main.enabled)
    {
        if (needsSource && zoneCount == 0)
            drySilenceReason = "NO_ZONES_LOADED";
        else if (! oscillatorEngineActive && zoneCount == 0 && activePartials == 0)
            drySilenceReason = "NO_SOUND_SOURCE";
        // NOTE (BUG 4): a nearest-root fallback (zoneCoverage == 2) is a VALID
        // selection — the probe always lands on a real sample when zones exist —
        // so it is NEVER reported as NO_ZONE_FOR_TEST_NOTE. A silent dry bus with
        // zones present is a gain/metering issue, attributed below.
        else if (voicePreLayerDb <= -60.0f)
            // Nothing came out of the voices at all — not a gain-stage problem.
            drySilenceReason = (mainLayerGainDb <= -40.0f) ? juce::String("MAIN_LAYER_GAIN_TOO_LOW")
                                                           : juce::String("NO_VOICE_OUTPUT");
        else if (postLayerDb <= -60.0f)
            // Voices were healthy but the layer-bus glue stage collapsed them.
            drySilenceReason = "LAYER_BUS_COLLAPSE";
        else if (masterGainDb <= -40.0f)
            // Post-layer signal is healthy; the master trim (driven by amp.gainDb)
            // is what silenced the dry output.
            drySilenceReason = (! masterGainMatchesAmp) ? juce::String("MASTER_GAIN_TOO_LOW")
                                                        : juce::String("AMP_GAIN_TOO_LOW");
        else
            drySilenceReason = "UNKNOWN_GAIN_COLLAPSE";

        warnings.add("DRY_BUS_SILENT");
        warnings.add("DRY_BUS_SILENT_REASON_" + drySilenceReason);
    }

    // Gain-stage attribution (Report 71/72): when an upstream stage is healthy
    // but the post-master dry bus is very low, name the exact stage that ate the
    // signal instead of leaving it ambiguous. This fires even when the bus is
    // not fully silent (e.g. -55 dB) so partial collapses surface. The probes
    // follow the real chain order: voice -> layerBus -> master.
    const bool upstreamHealthy = voicePreLayerDb > -40.0f || postLayerDb > -40.0f
                              || busPeakDb > -40.0f || fxInDb > -40.0f;
    if (upstreamHealthy && dryOutputDb <= -60.0f)
    {
        if (voicePreLayerDb > -40.0f && postLayerDb <= -60.0f)
            warnings.add("GAIN_STAGE_COLLAPSE_LAYER_BUS");
        else if (masterGainDb <= -40.0f)
            warnings.add("GAIN_STAGE_COLLAPSE_MASTER_GAIN");
        else if (postLayerDb <= -60.0f)
            warnings.add("GAIN_STAGE_COLLAPSE_PRE_MASTER");
        else
            warnings.add("GAIN_STAGE_COLLAPSE_METERING");
    }

    // FINAL_BUS_METER_MISMATCH: with every FX silenced the final output should
    // track the (master-gain-scaled) dry bus. Only flag a genuine COLLAPSE —
    // the final bus reading more than 12 dB BELOW a healthy dry bus (Report 71).
    // A hotter final (limiter/EQ makeup) or a quiet/silent dry bus is not a
    // routing fault, so those no longer trip the warning.
    const bool allFxOff = presetReverbSilenced && ! up.delay.enabled
                       && chorusMix <= 0.001f && satMix <= 0.001f;
    const bool dryBusHealthy = dryOutputDb > -60.0f;
    if (allFxOff && dryBusHealthy && finalOutputDb > -120.0f
        && (dryOutputDb - finalOutputDb) > 12.0f)
        warnings.add("FINAL_BUS_METER_MISMATCH");



    // -- Natural choir-mode state (sample-first vocal behavior) -------------
    const bool  choirNaturalMode             = naturalChoirSampleOnly;
    const float effectiveLayer2GainDb        = naturalChoirSampleOnly ? -120.0f
                                               : (up.layer2.enabled ? up.layer2.gainDb : -120.0f);
    const bool  choirSyntheticLayerDisabled  = choirMode && effectiveLayer2GainDb <= -120.0f;
    const float choirLayer2GainDb            = effectiveLayer2GainDb;
    const bool  choirNearestFallbackUsed     = choirMode
                                              && up.source.mappingMode.equalsIgnoreCase("nearest");
    const bool  choirZoneTooFar              = choirNearestFallbackUsed; // best signal we have at load time
    const float choirPitchShiftMaxSemis      = 0.0f; // populated by engine in future; reported for parity
    const float choirHumanizePitchCents      = choirMode ? juce::jmin(up.advanced.humanizePitchCents, 0.25f)
                                                         : up.advanced.humanizePitchCents;
    const float choirLayerDetuneCents        = choirMode ? 0.0f : up.main.detuneCents;
    const float choirOscBDetuneCents         = choirMode ? paramValue(proc, "oscBDetune") : up.layer2.detuneCents;
    const float choirUnisonDetune            = choirMode ? paramValue(proc, "unisonDetune") : 0.0f;
    const float choirVintageDriftCents       = choirMode ? paramValue(proc, "vintageAmount") * 2.5f : 0.0f;
    const bool  choirUnisonDisabled          = choirMode && paramValue(proc, "unisonVoices") <= 1.01f
                                            && std::abs(choirUnisonDetune) <= 0.0001f;
    const bool  choirAnalogDriftDisabled     = choirMode && std::abs(choirVintageDriftCents) <= 0.0001f;

    if (choirMode)
    {
        if (up.amp.releaseMs > 900.0f) warnings.add("CHOIR_RELEASE_TOO_LONG");
        if (reverbMix > 0.22f)         warnings.add("CHOIR_REVERB_TOO_WET");
        if (delayMix  > 0.03f || delayFeedback > 0.08f) warnings.add("CHOIR_DELAY_TOO_HIGH");
        if (choirActiveVoiceCount > 8) warnings.add("CHOIR_TOO_MANY_OVERLAPPING_VOICES");
        if (fxSendReleaseMsLive > 180.0f) warnings.add("CHOIR_FX_SEND_TOO_LONG");
        if (fxSendReleaseMsLive > 180.0f) warnings.add("CHOIR_FX_SEND_NOT_APPLIED");
        // BUG 5: a cap that did not need to fire is NOT a problem, so the old
        // CHOIR_*_CAP_NOT_APPLIED warnings are removed. choirReverbCapApplied /
        // choirDelayCapApplied now report only real before/after caps.

        // -- Natural-mode (sample-first) warnings --
        if (! choirSyntheticLayerDisabled && effectiveLayer2GainDb > -120.0f)
            warnings.add("CHOIR_SYNTH_LAYER_ACTIVE");
        if (finalDb > -10.0f) warnings.add("CHOIR_TOO_HOT");
        if (choirZoneTooFar)  warnings.add("CHOIR_ZONE_TOO_FAR");
        const float choirChorusCap = (nLow.contains("wide") || nLow.contains("heaven")) ? 0.015f : 0.0f;
        if (chorusMix > choirChorusCap + 0.001f) warnings.add("CHOIR_CHORUS_TOO_HIGH");
        if (std::abs(choirHumanizePitchCents) > 0.25f
            || std::abs(choirLayerDetuneCents) > 0.25f
            || std::abs(choirOscBDetuneCents) > 0.25f
            || std::abs(choirUnisonDetune) > 0.25f
            || std::abs(choirVintageDriftCents) > 0.25f)
            warnings.add("CHOIR_DETUNE_TOO_HIGH");
    }

    // --- Loudness calibration (suggestion-only) ---------------------------
    const auto target = loudnessTargetForCategory(effectiveCategory);
    const float targetCenterDb = 0.5f * (target.minDb + target.maxDb);
    const bool notesPlaying = finalDb > -100.0f;
    float suggestedGainDb = 0.0f;
    if (notesPlaying)
    {
        suggestedGainDb = targetCenterDb - finalDb;
        // Clamp suggestion to a sensible range so a one-off silent frame
        // doesn't recommend +60dB.
        suggestedGainDb = juce::jlimit(-24.0f, 24.0f, suggestedGainDb);

        if (finalDb < target.minDb)
        {
            // Replace any prior generic TOO_QUIET so we don't double-warn.
            warnings.removeString("TOO_QUIET");
            warnings.add("TOO_QUIET");
        }
        if (finalDb > target.maxDb && finalDb <= -1.0f)
            warnings.add("LOW_HEADROOM");
    }


    // POSSIBLE_BEEP_LAYER: reinforcement-style layer that is loud enough to
    // poke through as a tone on top of the sampled instrument.
    auto isBeepyRole = [](const juce::String& role)
    {
        const auto r = role.toLowerCase();
        return r == "air" || r == "warmth" || r == "texture" || r == "sub" || r.isEmpty();
    };
    bool beep = false;
    if (up.layer2.enabled && up.layer2.gainDb > -18.0f && isBeepyRole(up.layer2.eqRole))
        beep = true;
    for (auto& p : up.partials)
        if (p.enabled && isBeepyRole(p.eqRole))
        {
            const float g = juce::Decibels::gainToDecibels(juce::jmax(1.0e-6f, p.level));
            if (g > -18.0f) { beep = true; break; }
        }
    if (beep) warnings.add("POSSIBLE_BEEP_LAYER");

    const juce::String rawPath = sourceInstrumentPathRaw.isNotEmpty()
                               ? sourceInstrumentPathRaw : up.source.path;

    // --- Dedupe: suppress identical reloads within 300ms ------------------
    auto& sess = sessionState();
    const juce::String dedupeKey = up.presetName + "|" + rawPath + "|" + effectiveCategory;
    const juce::int64 nowMs = juce::Time::currentTimeMillis();
    if (sess.lastPresetKey == dedupeKey && (nowMs - sess.lastLoadMs) < 300)
        return;
    sess.lastPresetKey = dedupeKey;
    sess.lastLoadMs    = nowMs;
    const int loadIndex = ++sess.loadIndex;

    const juce::String timestamp = juce::Time::getCurrentTime().toISO8601(true);
    const juce::String pluginVersion =
       #ifdef JucePlugin_VersionString
        JucePlugin_VersionString;
       #else
        "unknown";
       #endif
    const juce::String browserPresetName = browserPresetNameIn.isNotEmpty()
                                           ? browserPresetNameIn : up.presetName;
    const juce::String bankCategory = bankCategoryIn.isNotEmpty()
                                      ? bankCategoryIn : effectiveCategory;

    // --- Emit -------------------------------------------------------------
    juce::String out;
    out << "[DIDITAGAIN preset-quality]"
        << " sessionId=" << sess.sessionId
        << " presetLoadIndex=" << loadIndex
        << " presetName=" << up.presetName
        << " browserPresetName=" << browserPresetName
        << " category=" << effectiveCategory
        << " bankCategory=" << bankCategory
        << " engineType=" << engineType
        << " presetFilePath=" << presetFilePathIn
        << " presetCategoryFolder=" << presetCategoryFolderIn
        << " expectedSourceFolderName=" << expectedSourceFolderNameIn
        << " sourceInstrumentPathRaw=" << rawPath
        << " resolvedFolder=" << resolvedFolderPath
        << " resolvedFrom=" << resolvedFrom
        << " hiddenSourceFolder=" << (hiddenSourceFolder ? "true" : "false")
        << " allowCrossCategorySource=" << (allowCrossCategorySourceIn ? "true" : "false")
        << " sourceFolderWavCount=" << sourceFolderWavCountIn
        << " sourceRequiredForEngine=" << (needsSource ? "true" : "false")
        << " oscillatorEngineActive=" << (oscillatorEngineActive ? "true" : "false")
        << " wavZones=" << wavZones
        << " activeLayers=" << activeLayers
        << " activePartials=" << activePartials
        << " layerBusPeak=" << dbStr(juce::Decibels::decibelsToGain(busPeakDb))
        << "dB fxInputPeak=" << juce::String(fxInDb, 2)
        << "dB fxOutputPeak=" << juce::String(fxOutDb, 2)
        << "dB finalPeak=" << juce::String(finalDb, 2)
        << "dB dryOutputPeakDb=" << juce::String(dryOutputDb, 2)
        << " reverbReturnPeakDb=" << juce::String(reverbReturnDb, 2)
        << " delayReturnPeakDb=" << juce::String(delayReturnDb, 2)
        << " finalOutputPeakDb=" << juce::String(finalOutputDb, 2)
        << " dryVoicePreLayerPeakDb=" << juce::String(voicePreLayerDb, 2)
        << " postLayerPeakDb=" << juce::String(postLayerDb, 2)
        << " dryRawPeakDb=" << juce::String(dryRawPeakDb, 2)
        << " masterGainDb=" << juce::String(masterGainDb, 2)
        << " ampGainDb=" << juce::String(ampGainDb, 2)
        << " mainLayerGainDb=" << juce::String(mainLayerGainDb, 2)
        << " layer2GainStageDb=" << juce::String(layer2GainStageDb, 2)
        << " masterMinusAmpDb=" << juce::String(masterMinusAmpDb, 2)
        << " masterGainMatchesAmp=" << (masterGainMatchesAmp ? "true" : "false")
        << " presetJsonAmpGainDb=" << juce::String(presetJsonAmpGainDb, 2)
        << " appliedAmpGainDb=" << juce::String(appliedAmpGainDb, 2)
        << " presetJsonMasterGainDb=" << juce::String(presetJsonMasterGainDb, 2)
        << " appliedMasterGainDb=" << juce::String(appliedMasterGainDb, 2)
        << " autoNormalizeGainDb=" << juce::String(autoNormalizeGainDb, 2)
        << " limiterGainReductionDb=" << juce::String(limiterGainReductionDb, 2)
        << " masterGainSource=" << masterGainSource
        << " ampGainSource=" << ampGainSource
        << " intentionalMute=" << (intentionalMute ? "true" : "false")
        << " voicePreAmpPeakDb=" << juce::String(voicePreAmpDb, 2)
        << " dryOutputExpectedDb=" << juce::String(dryOutputExpectedDb, 2)
        << " silenceTestNote=" << silenceTestNote
        << " firstZoneRoot=" << firstZoneRoot
        << " lastZoneRoot=" << lastZoneRoot
        << " selectedZoneRoot=" << selectedZoneRoot
        << " selectedZoneFile=" << (selectedZoneFile.isNotEmpty() ? selectedZoneFile : juce::String("none"))
        << " zoneFallbackUsed=" << (zoneFallbackUsed ? "true" : "false")
        << " zoneDistanceSemitones=" << zoneDistanceSemitones
        << " activeZoneCount=" << zoneCount
        << " testNoteZoneCoverage=" << (zoneCoverage == 0 ? "none"
                                        : zoneCoverage == 1 ? "exact" : "nearestFallback")
        << " drySilenceReason=" << (drySilenceReason.isNotEmpty() ? drySilenceReason : juce::String("none"))
        << " presetJsonReverbMix=" << fmt(presetJsonReverbMix)
        << " appliedReverbMix=" << fmt(appliedReverbMix)
        << " presetJsonDelayMix=" << fmt(presetJsonDelayMix)
        << " appliedDelayMix=" << fmt(appliedDelayMix)
        << " presetJsonDelayFeedback=" << fmt(presetJsonDelayFeedback)
        << " appliedDelayFeedback=" << fmt(appliedDelayFeedback)
        << " presetJsonFxSendReleaseMs=" << juce::String(presetJsonFxSendReleaseMs, 1)
        << " appliedFxSendReleaseMs=" << juce::String(appliedFxSendReleaseMs, 1)
        << " presetValueMismatchFields=[" << presetValueMismatchFields.joinIntoString(",") << "]"
        << " clampedFields=" << "n/a"
        << " reverbMix=" << fmt(reverbMix)
        << " delayMix=" << fmt(delayMix)
        << " chorusMix=" << fmt(chorusMix)
        << " saturationMix=" << fmt(satMix)
        << " reverbDuckingEnabled=" << (reverbDuckEnabled ? "true" : "false")
        << " reverbDuckingAmount=" << fmt(reverbDuckAmount)
        << " reverbInputHighpassHz=" << juce::String(reverbInputHpHz, 1)
        << " reverbInputLowpassHz=" << juce::String(reverbInputLpHz, 1)
        << " delayDuckingEnabled=" << (delayDuckEnabled ? "true" : "false")
        << " delayDuckingAmount=" << fmt(delayDuckAmount)
        << " delayFeedback=" << fmt(delayFeedback)
        << " scaleSafeFxMode=" << (scaleSafeFxMode ? "true" : "false")
        << " noteDensityFxReduction=" << (noteDensityFxOn ? "true" : "false")
        << " fxTailClearOnPresetChange=" << (fxTailClearOnLoad ? "true" : "false")
        << " fxSendPostEnvelope=true"
        << " fxSendFollowsAmpEnvelope=true"
        << " fxSendReleaseMs=" << juce::String(fxSendReleaseMsLive, 1)
        << " fxSendReleaseSource=" << fxSendReleaseSource
        << " noteOffStopsFxSend=true"
        << " clearFxOnTransportStop=true"
        << " transportStopFxFadeMs=120"
        << " clearFxTailOnPresetChange=" << (fxTailClearOnLoad ? "true" : "false")
        << " choirMode=" << (choirMode ? "true" : "false")
        << " choirAmpReleaseClamped=" << (choirAmpReleaseClamped ? "true" : "false")
        << " choirFxSendReleaseMs=" << juce::String(choirFxSendReleaseMs, 1)
        << " choirReverbCapApplied=" << (choirReverbCapApplied ? "true" : "false")
        << " choirDelayCapApplied=" << (choirDelayCapApplied ? "true" : "false")
        << " choirCapFieldsApplied=[" << choirCapFieldsApplied.joinIntoString(",") << "]"
        << " choirNoteDensityFxReduction=" << (choirNoteDensityFxReduction ? "true" : "false")
        << " choirActiveVoiceCount=" << choirActiveVoiceCount
        << " choirFxInputAfterNoteOffDb=" << juce::String(fxInDb, 2)
        << " choirNaturalMode=" << (choirNaturalMode ? "true" : "false")
        << " choirSyntheticLayerDisabled=" << (choirSyntheticLayerDisabled ? "true" : "false")
        << " choirLayer2GainDb=" << juce::String(choirLayer2GainDb, 2)
        << " choirPitchShiftMaxSemis=" << juce::String(choirPitchShiftMaxSemis, 2)
        << " choirNearestFallbackUsed=" << (choirNearestFallbackUsed ? "true" : "false")
        << " choirZoneTooFar=" << (choirZoneTooFar ? "true" : "false")
        << " choirHumanizePitchCents=" << juce::String(choirHumanizePitchCents, 2)
        << " choirLayerDetuneCents=" << juce::String(choirLayerDetuneCents, 2)
        << " choirOscBDetuneCents=" << juce::String(choirOscBDetuneCents, 2)
        << " choirUnisonDetune=" << juce::String(choirUnisonDetune, 3)
        << " choirVintageDriftCents=" << juce::String(choirVintageDriftCents, 2)
        << " choirUnisonDisabled=" << (choirUnisonDisabled ? "true" : "false")
        << " choirAnalogDriftDisabled=" << (choirAnalogDriftDisabled ? "true" : "false")
        << " layer2Gain=" << fmt(up.layer2.gainDb, 2) << "dB"
        << " layer2BlendMode=" << (up.layer2.blendMode.isNotEmpty() ? up.layer2.blendMode : juce::String("auto"))
        << " layer2EqRole=" << (up.layer2.eqRole.isNotEmpty() ? up.layer2.eqRole : juce::String("auto"))
        << " followMainEnvelope=" << (up.layer2.followMainEnvelope ? "true" : "false")
        << " polyphony=" << polyphony
        << " estimatedHeadroomDb=" << juce::String(headroomDb, 2)
        << " categoryTargetMinDb=" << juce::String(target.minDb, 2)
        << " categoryTargetMaxDb=" << juce::String(target.maxDb, 2)
        << " suggestedGainAdjustmentDb=" << (notesPlaying ? juce::String(suggestedGainDb, 2) : juce::String("n/a"))
        << " pluginVersion=" << pluginVersion
        << " timestamp=" << timestamp
        << " warnings=" << (warnings.isEmpty() ? juce::String("none")
                                               : warnings.joinIntoString(","));
    juce::Logger::writeToLog(out);

    // --- Build JSON object for latest + jsonl session log -----------------
    juce::DynamicObject::Ptr j = new juce::DynamicObject();
    j->setProperty("sessionId",              sess.sessionId);
    j->setProperty("presetLoadIndex",        loadIndex);
    j->setProperty("pluginVersion",          pluginVersion);
    j->setProperty("presetName",             up.presetName);
    j->setProperty("browserPresetName",      browserPresetName);
    j->setProperty("category",               effectiveCategory);
    j->setProperty("bankCategory",           bankCategory);
    j->setProperty("categoryRaw",            effectiveCategoryIn);
    j->setProperty("engineType",             engineType);
    j->setProperty("oscillatorEngineActive", oscillatorEngineActive);
    j->setProperty("sourceRequiredForEngine", needsSource);
    j->setProperty("sourceInstrument",       rawPath);
    j->setProperty("sourceInstrumentPathRaw", rawPath);
    j->setProperty("resolvedFolder",         resolvedFolderPath);
    j->setProperty("resolvedFrom",           resolvedFrom);
    j->setProperty("hiddenSourceFolder",     hiddenSourceFolder);
    j->setProperty("presetFilePath",         presetFilePathIn);
    j->setProperty("presetCategoryFolder",   presetCategoryFolderIn);
    j->setProperty("expectedSourceFolderName", expectedSourceFolderNameIn);
    j->setProperty("allowCrossCategorySource", allowCrossCategorySourceIn);
    j->setProperty("sourceFolderWavCount",   sourceFolderWavCountIn);
    j->setProperty("wavZones",               wavZones);
    j->setProperty("activeLayers",           activeLayers);
    j->setProperty("activePartials",         activePartials);
    j->setProperty("layerBusPeakDb",         busPeakDb);
    j->setProperty("fxInputPeakDb",          fxInDb);
    j->setProperty("fxOutputPeakDb",         fxOutDb);
    j->setProperty("finalPeakDb",            finalDb);
    j->setProperty("dryOutputPeakDb",        dryOutputDb);
    j->setProperty("reverbReturnPeakDb",     reverbReturnDb);
    j->setProperty("delayReturnPeakDb",      delayReturnDb);
    j->setProperty("finalOutputPeakDb",      finalOutputDb);
    j->setProperty("dryVoicePreLayerPeakDb", voicePreLayerDb);
    j->setProperty("postLayerPeakDb",        postLayerDb);
    j->setProperty("dryRawPeakDb",           dryRawPeakDb);
    j->setProperty("masterGainDb",           masterGainDb);
    j->setProperty("ampGainDb",              ampGainDb);
    j->setProperty("mainLayerGainDb",        mainLayerGainDb);
    j->setProperty("layer2GainStageDb",      layer2GainStageDb);
    j->setProperty("masterMinusAmpDb",       masterMinusAmpDb);
    j->setProperty("masterGainMatchesAmp",   masterGainMatchesAmp);
    j->setProperty("presetJsonAmpGainDb",    presetJsonAmpGainDb);
    j->setProperty("appliedAmpGainDb",       appliedAmpGainDb);
    j->setProperty("presetJsonMasterGainDb", presetJsonMasterGainDb);
    j->setProperty("appliedMasterGainDb",    appliedMasterGainDb);
    j->setProperty("autoNormalizeGainDb",    autoNormalizeGainDb);
    j->setProperty("limiterGainReductionDb", limiterGainReductionDb);
    j->setProperty("masterGainSource",       masterGainSource);
    j->setProperty("ampGainSource",          ampGainSource);
    j->setProperty("intentionalMute",        intentionalMute);
    j->setProperty("voicePreAmpPeakDb",      voicePreAmpDb);
    j->setProperty("dryOutputExpectedDb",    dryOutputExpectedDb);
    j->setProperty("silenceTestNote",        silenceTestNote);
    j->setProperty("firstZoneRoot",          firstZoneRoot);
    j->setProperty("lastZoneRoot",           lastZoneRoot);
    j->setProperty("selectedZoneRoot",       selectedZoneRoot);
    j->setProperty("selectedZoneFile",       selectedZoneFile.isNotEmpty() ? selectedZoneFile : juce::String("none"));
    j->setProperty("zoneFallbackUsed",       zoneFallbackUsed);
    j->setProperty("zoneDistanceSemitones",  zoneDistanceSemitones);
    j->setProperty("activeZoneCount",        zoneCount);
    j->setProperty("testNoteZoneCoverage",   zoneCoverage == 0 ? "none"
                                             : zoneCoverage == 1 ? "exact" : "nearestFallback");
    j->setProperty("drySilenceReason",       drySilenceReason.isNotEmpty() ? drySilenceReason : juce::String("none"));
    j->setProperty("presetJsonReverbMix",    presetJsonReverbMix);
    j->setProperty("appliedReverbMix",       appliedReverbMix);
    j->setProperty("presetJsonDelayMix",     presetJsonDelayMix);
    j->setProperty("appliedDelayMix",        appliedDelayMix);
    j->setProperty("presetJsonDelayFeedback", presetJsonDelayFeedback);
    j->setProperty("appliedDelayFeedback",   appliedDelayFeedback);
    j->setProperty("presetJsonFxSendReleaseMs", presetJsonFxSendReleaseMs);
    j->setProperty("appliedFxSendReleaseMs", appliedFxSendReleaseMs);
    {
        juce::Array<juce::var> mmVar;
        for (auto& f : presetValueMismatchFields) mmVar.add(f);
        j->setProperty("presetValueMismatchFields", mmVar);
    }
    j->setProperty("presetReverbSilenced",   presetReverbSilenced);
    j->setProperty("clampedFields",          "n/a");
    j->setProperty("reverbMix",              reverbMix);
    j->setProperty("delayMix",               delayMix);
    j->setProperty("chorusMix",              chorusMix);
    j->setProperty("saturationMix",          satMix);
    j->setProperty("reverbDuckingEnabled",   reverbDuckEnabled);
    j->setProperty("reverbDuckingAmount",    reverbDuckAmount);
    j->setProperty("reverbInputHighpassHz",  reverbInputHpHz);
    j->setProperty("reverbInputLowpassHz",   reverbInputLpHz);
    j->setProperty("delayDuckingEnabled",    delayDuckEnabled);
    j->setProperty("delayDuckingAmount",     delayDuckAmount);
    j->setProperty("delayFeedback",          delayFeedback);
    j->setProperty("scaleSafeFxMode",        scaleSafeFxMode);
    j->setProperty("noteDensityFxReduction", noteDensityFxOn);
    j->setProperty("fxTailClearOnPresetChange", fxTailClearOnLoad);
    j->setProperty("fxSendPostEnvelope",      true);
    j->setProperty("fxSendFollowsAmpEnvelope", true);
    j->setProperty("fxSendReleaseMs",         fxSendReleaseMsLive);
    j->setProperty("fxSendReleaseSource",     fxSendReleaseSource);
    j->setProperty("noteOffStopsFxSend",      true);
    j->setProperty("clearFxOnTransportStop",  true);
    j->setProperty("transportStopFxFadeMs",   120);
    j->setProperty("clearFxTailOnPresetChange", fxTailClearOnLoad);
    j->setProperty("choirMode",                 choirMode);
    j->setProperty("choirAmpReleaseClamped",    choirAmpReleaseClamped);
    j->setProperty("choirFxSendReleaseMs",      choirFxSendReleaseMs);
    j->setProperty("choirReverbCapApplied",     choirReverbCapApplied);
    j->setProperty("choirDelayCapApplied",      choirDelayCapApplied);
    {
        juce::Array<juce::var> capVar;
        for (auto& f : choirCapFieldsApplied) capVar.add(f);
        j->setProperty("choirCapFieldsApplied", capVar);
    }
    j->setProperty("choirNoteDensityFxReduction", choirNoteDensityFxReduction);
    j->setProperty("choirActiveVoiceCount",     choirActiveVoiceCount);
    j->setProperty("choirFxInputAfterNoteOffDb", fxInDb);
    j->setProperty("choirNaturalMode",            choirNaturalMode);
    j->setProperty("choirSyntheticLayerDisabled", choirSyntheticLayerDisabled);
    j->setProperty("choirLayer2GainDb",           choirLayer2GainDb);
    j->setProperty("choirPitchShiftMaxSemis",     choirPitchShiftMaxSemis);
    j->setProperty("choirNearestFallbackUsed",    choirNearestFallbackUsed);
    j->setProperty("choirZoneTooFar",             choirZoneTooFar);
    j->setProperty("choirHumanizePitchCents",     choirHumanizePitchCents);
    j->setProperty("choirLayerDetuneCents",       choirLayerDetuneCents);
    j->setProperty("choirOscBDetuneCents",        choirOscBDetuneCents);
    j->setProperty("choirUnisonDetune",           choirUnisonDetune);
    j->setProperty("choirVintageDriftCents",      choirVintageDriftCents);
    j->setProperty("choirUnisonDisabled",         choirUnisonDisabled);
    j->setProperty("choirAnalogDriftDisabled",    choirAnalogDriftDisabled);
    j->setProperty("layer2GainDb",           up.layer2.gainDb);
    j->setProperty("layer2BlendMode",        up.layer2.blendMode.isNotEmpty() ? up.layer2.blendMode : juce::String("auto"));
    j->setProperty("layer2EqRole",           up.layer2.eqRole.isNotEmpty() ? up.layer2.eqRole : juce::String("auto"));
    j->setProperty("followMainEnvelope",     up.layer2.followMainEnvelope);
    j->setProperty("polyphony",              polyphony);
    j->setProperty("estimatedHeadroomDb",    headroomDb);
    j->setProperty("categoryTargetMinDb",    target.minDb);
    j->setProperty("categoryTargetMaxDb",    target.maxDb);
    if (notesPlaying)
        j->setProperty("suggestedGainAdjustmentDb", suggestedGainDb);
    else
        j->setProperty("suggestedGainAdjustmentDb", juce::var());
    juce::Array<juce::var> warnVar;
    for (auto& w : warnings) warnVar.add(w);
    j->setProperty("warnings",               warnVar);
    j->setProperty("timestamp",              timestamp);

    // Single-line JSON for jsonl, pretty for the "latest" snapshot.
    const juce::String jsonLine   = juce::JSON::toString(juce::var(j.get()), true);
    const juce::String jsonPretty = juce::JSON::toString(juce::var(j.get()), false);

    // Robust append: FileOutputStream in append mode with explicit flush so
    // every preset load is durably written to the session logs even if the
    // host crashes shortly after. juce::File::appendText() has been observed
    // to silently no-op on Windows when another process holds a read lock.
    auto appendUtf8 = [](const juce::File& f, const juce::String& text)
    {
        auto dir = f.getParentDirectory();
        if (! dir.isDirectory()) dir.createDirectory();
        if (! f.existsAsFile()) f.create();
        juce::FileOutputStream os (f);
        if (! os.openedOk())
        {
            DBG("[DIDITAGAIN preset-quality] append FAILED file=" << f.getFullPathName());
            return;
        }
        os.setPosition(os.getFile().getSize());
        auto utf8 = text.toRawUTF8();
        os.write(utf8, std::strlen(utf8));
        os.flush();
    };

    // 1) latest_preset_quality.json — overwrite with most recent only.
    {
        auto f = latestJsonFile();
        auto dir = f.getParentDirectory();
        if (! dir.isDirectory()) dir.createDirectory();
        if (! f.replaceWithText(jsonPretty))
        {
            juce::Logger::writeToLog("[DIDITAGAIN preset-quality] latest write FAILED file="
                                     + f.getFullPathName());
        }
    }

    // 2) preset_quality_session.jsonl — append one line per load.
    appendUtf8(sessionJsonlFile(), jsonLine + "\n");

    // 3) preset_quality_session.txt — append human-readable block.
    {
        juce::String block;
        block << "==================================================\n"
              << "[DIDITAGAIN preset-quality]\n"
              << "sessionId: "                 << sess.sessionId       << "\n"
              << "presetLoadIndex: "           << loadIndex            << "\n"
              << "pluginVersion: "             << pluginVersion        << "\n"
              << "presetName: "                << up.presetName        << "\n"
              << "browserPresetName: "         << browserPresetName    << "\n"
              << "category: "                  << effectiveCategory    << "\n"
              << "bankCategory: "              << bankCategory         << "\n"
              << "engineType: "                << engineType           << "\n"
              << "sourceInstrumentPathRaw: "   << rawPath              << "\n"
              << "resolvedFolder: "            << resolvedFolderPath   << "\n"
              << "resolvedFrom: "              << resolvedFrom         << "\n"
              << "hiddenSourceFolder: "        << (hiddenSourceFolder ? "true" : "false") << "\n"
              << "presetFilePath: "            << presetFilePathIn         << "\n"
              << "presetCategoryFolder: "      << presetCategoryFolderIn   << "\n"
              << "expectedSourceFolderName: "  << expectedSourceFolderNameIn << "\n"
              << "allowCrossCategorySource: "  << (allowCrossCategorySourceIn ? "true" : "false") << "\n"
              << "sourceFolderWavCount: "      << sourceFolderWavCountIn   << "\n"
              << "sourceRequiredForEngine: "   << (needsSource ? "true" : "false") << "\n"
              << "wavZones: "                  << wavZones             << "\n"
              << "activeLayers: "              << activeLayers         << "\n"
              << "activePartials: "            << activePartials       << "\n"
              << "layerBusPeakDb: "            << juce::String(busPeakDb, 2) << "\n"
              << "fxInputPeakDb: "             << juce::String(fxInDb, 2)    << "\n"
              << "fxOutputPeakDb: "            << juce::String(fxOutDb, 2)   << "\n"
              << "finalPeakDb: "               << juce::String(finalDb, 2)   << "\n"
              << "dryOutputPeakDb: "           << juce::String(dryOutputDb, 2) << "\n"
              << "reverbReturnPeakDb: "        << juce::String(reverbReturnDb, 2) << "\n"
              << "delayReturnPeakDb: "         << juce::String(delayReturnDb, 2) << "\n"
              << "finalOutputPeakDb: "         << juce::String(finalOutputDb, 2) << "\n"
              << "dryVoicePreLayerPeakDb: "    << juce::String(voicePreLayerDb, 2) << "\n"
              << "postLayerPeakDb: "           << juce::String(postLayerDb, 2) << "\n"
              << "dryRawPeakDb: "              << juce::String(dryRawPeakDb, 2) << "\n"
              << "masterGainDb: "              << juce::String(masterGainDb, 2) << "\n"
              << "ampGainDb: "                 << juce::String(ampGainDb, 2) << "\n"
              << "mainLayerGainDb: "           << juce::String(mainLayerGainDb, 2) << "\n"
              << "layer2GainStageDb: "         << juce::String(layer2GainStageDb, 2) << "\n"
              << "masterMinusAmpDb: "          << juce::String(masterMinusAmpDb, 2) << "\n"
              << "masterGainMatchesAmp: "      << (masterGainMatchesAmp ? "true" : "false") << "\n"
              << "presetJsonAmpGainDb: "       << juce::String(presetJsonAmpGainDb, 2) << "\n"
              << "appliedAmpGainDb: "          << juce::String(appliedAmpGainDb, 2) << "\n"
              << "presetJsonMasterGainDb: "    << juce::String(presetJsonMasterGainDb, 2) << "\n"
              << "appliedMasterGainDb: "       << juce::String(appliedMasterGainDb, 2) << "\n"
              << "autoNormalizeGainDb: "       << juce::String(autoNormalizeGainDb, 2) << "\n"
              << "limiterGainReductionDb: "    << juce::String(limiterGainReductionDb, 2) << "\n"
              << "masterGainSource: "          << masterGainSource << "\n"
              << "ampGainSource: "             << ampGainSource << "\n"
              << "intentionalMute: "           << (intentionalMute ? "true" : "false") << "\n"
              << "voicePreAmpPeakDb: "         << juce::String(voicePreAmpDb, 2) << "\n"
              << "dryOutputExpectedDb: "       << juce::String(dryOutputExpectedDb, 2) << "\n"
              << "drySilenceReason: "          << (drySilenceReason.isNotEmpty() ? drySilenceReason : juce::String("none")) << "\n"
              << "firstZoneRoot: "             << firstZoneRoot << "\n"
              << "lastZoneRoot: "              << lastZoneRoot << "\n"
              << "selectedZoneRoot: "          << selectedZoneRoot << "\n"
              << "selectedZoneFile: "          << (selectedZoneFile.isNotEmpty() ? selectedZoneFile : juce::String("none")) << "\n"
              << "zoneFallbackUsed: "          << (zoneFallbackUsed ? "true" : "false") << "\n"
              << "zoneDistanceSemitones: "     << zoneDistanceSemitones << "\n"
              << "presetJsonReverbMix: "       << juce::String(presetJsonReverbMix, 3) << "\n"
              << "appliedReverbMix: "          << juce::String(appliedReverbMix, 3) << "\n"
              << "presetJsonDelayMix: "        << juce::String(presetJsonDelayMix, 3) << "\n"
              << "appliedDelayMix: "           << juce::String(appliedDelayMix, 3) << "\n"
              << "presetJsonDelayFeedback: "   << juce::String(presetJsonDelayFeedback, 3) << "\n"
              << "appliedDelayFeedback: "      << juce::String(appliedDelayFeedback, 3) << "\n"
              << "presetJsonFxSendReleaseMs: " << juce::String(presetJsonFxSendReleaseMs, 1) << "\n"
              << "appliedFxSendReleaseMs: "    << juce::String(appliedFxSendReleaseMs, 1) << "\n"
              << "presetValueMismatchFields: " << (presetValueMismatchFields.isEmpty() ? juce::String("none") : presetValueMismatchFields.joinIntoString(",")) << "\n"
              << "presetReverbSilenced: "      << (presetReverbSilenced ? "true" : "false") << "\n"
              << "reverbDuckingEnabled: "      << (reverbDuckEnabled ? "true" : "false") << "\n"
              << "reverbDuckingAmount: "       << juce::String(reverbDuckAmount, 3) << "\n"
              << "reverbInputHighpassHz: "     << juce::String(reverbInputHpHz, 1) << "\n"
              << "reverbInputLowpassHz: "      << juce::String(reverbInputLpHz, 1) << "\n"
              << "delayDuckingEnabled: "       << (delayDuckEnabled ? "true" : "false") << "\n"
              << "delayDuckingAmount: "        << juce::String(delayDuckAmount, 3) << "\n"
              << "delayFeedback: "             << juce::String(delayFeedback, 3) << "\n"
              << "scaleSafeFxMode: "           << (scaleSafeFxMode ? "true" : "false") << "\n"
              << "noteDensityFxReduction: "    << (noteDensityFxOn ? "true" : "false") << "\n"
              << "fxTailClearOnPresetChange: " << (fxTailClearOnLoad ? "true" : "false") << "\n"
              << "fxSendPostEnvelope: true\n"
              << "fxSendFollowsAmpEnvelope: true\n"
              << "fxSendReleaseMs: "           << juce::String(fxSendReleaseMsLive, 1) << "\n"
              << "fxSendReleaseSource: "       << fxSendReleaseSource << "\n"
              << "noteOffStopsFxSend: true\n"
              << "clearFxOnTransportStop: true\n"
              << "transportStopFxFadeMs: 120\n"
              << "clearFxTailOnPresetChange: " << (fxTailClearOnLoad ? "true" : "false") << "\n"
              << "choirMode: "                 << (choirMode ? "true" : "false") << "\n"
              << "choirAmpReleaseClamped: "    << (choirAmpReleaseClamped ? "true" : "false") << "\n"
              << "choirFxSendReleaseMs: "      << juce::String(choirFxSendReleaseMs, 1) << "\n"
              << "choirReverbCapApplied: "     << (choirReverbCapApplied ? "true" : "false") << "\n"
              << "choirDelayCapApplied: "      << (choirDelayCapApplied ? "true" : "false") << "\n"
              << "choirNoteDensityFxReduction: " << (choirNoteDensityFxReduction ? "true" : "false") << "\n"
              << "choirActiveVoiceCount: "     << choirActiveVoiceCount << "\n"
              << "choirFxInputAfterNoteOffDb: " << juce::String(fxInDb, 2) << "\n"
              << "choirNaturalMode: "            << (choirNaturalMode ? "true" : "false") << "\n"
              << "choirSyntheticLayerDisabled: " << (choirSyntheticLayerDisabled ? "true" : "false") << "\n"
              << "choirLayer2GainDb: "           << juce::String(choirLayer2GainDb, 2) << "\n"
              << "choirPitchShiftMaxSemis: "     << juce::String(choirPitchShiftMaxSemis, 2) << "\n"
              << "choirNearestFallbackUsed: "    << (choirNearestFallbackUsed ? "true" : "false") << "\n"
              << "choirZoneTooFar: "             << (choirZoneTooFar ? "true" : "false") << "\n"
              << "choirHumanizePitchCents: "     << juce::String(choirHumanizePitchCents, 2) << "\n"
              << "choirLayerDetuneCents: "       << juce::String(choirLayerDetuneCents, 2) << "\n"
              << "choirOscBDetuneCents: "        << juce::String(choirOscBDetuneCents, 2) << "\n"
              << "choirUnisonDetune: "           << juce::String(choirUnisonDetune, 3) << "\n"
              << "choirVintageDriftCents: "      << juce::String(choirVintageDriftCents, 2) << "\n"
              << "choirUnisonDisabled: "         << (choirUnisonDisabled ? "true" : "false") << "\n"
              << "choirAnalogDriftDisabled: "    << (choirAnalogDriftDisabled ? "true" : "false") << "\n"
              << "categoryTargetMinDb: "       << juce::String(target.minDb, 2) << "\n"
              << "categoryTargetMaxDb: "       << juce::String(target.maxDb, 2) << "\n"
              << "suggestedGainAdjustmentDb: " << (notesPlaying ? juce::String(suggestedGainDb, 2) : juce::String("n/a")) << "\n"
              << "warnings: "                  << (warnings.isEmpty() ? juce::String("none") : warnings.joinIntoString(",")) << "\n"
              << "timestamp: "                 << timestamp            << "\n"
              << "==================================================\n";
        appendUtf8(sessionTextFile(), block);
    }

    DBG("[DIDITAGAIN preset-quality] appended idx=" << loadIndex
        << " preset=" << up.presetName
        << " jsonl=" << sessionJsonlFile().getFullPathName());
}

}} // namespace dida::presetreport
