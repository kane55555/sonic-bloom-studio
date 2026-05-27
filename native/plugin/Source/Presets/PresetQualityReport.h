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
    if (c.contains("string") || c.contains("choir") || c.contains("vox")
        || c.contains("vocal") || c.contains("pad"))
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

    // --- Layer / partial counts -------------------------------------------
    int activeLayers = (up.main.enabled ? 1 : 0) + (up.layer2.enabled ? 1 : 0);
    int activePartials = 0;
    for (auto& p : up.partials) if (p.enabled) ++activePartials;

    const juce::String engineType = up.engineType.isNotEmpty() ? up.engineType
                                  : (up.partials.size() > 0 ? juce::String("layered") : juce::String("pcm"));

    const bool needsSource = engineRequiresSource(up);
    const bool oscillatorEngineActive = ! needsSource;

    // --- FX mixes (read live from APVTS) ----------------------------------
    const float chorusMix = paramValue(proc, "fxChorusMix");
    const float delayMix  = paramValue(proc, "fxDelayMix");
    const float reverbMix = paramValue(proc, "fxReverbMix");
    const float reverbSize = paramValue(proc, "fxReverbSize");
    const float satMix    = paramValue(proc, "fxSaturationMix");
    const int   polyphony = (int) paramValue(proc, "polyphony");

    // --- Live FX-chain state for scale-safety report ----------------------
    auto& reverbBlk = fx.getReverb();
    auto& delayBlk  = fx.getDelay();
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

    const auto cLow = effectiveCategory.toLowerCase();
    const auto nLow = up.presetName.toLowerCase();
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
    const bool choirMode = cLow.contains("choir") || cLow.contains("vox")
                         || cLow.contains("vocal") || nLow.contains("choir") || up.choirMode;
    const float fxSendReleaseMsLive = proc.getSynthEngine().getFxSendReleaseMs();
    const auto choirFxSendCap = up.safety.hasChoirFxSendReleaseMaxMs
        ? up.safety.choirFxSendReleaseMaxMs
        : (up.fxSend.hasFxSendMaximumReleaseMs ? up.fxSend.fxSendMaximumReleaseMs : 180.0f);
    const float choirFxSendReleaseMs = choirMode
        ? juce::jlimit(40.0f, juce::jlimit(40.0f, 180.0f, choirFxSendCap),
                       up.fxSend.hasFxSendReleaseMs ? up.fxSend.fxSendReleaseMs
                                                    : juce::jmin(choirFxSendCap, up.amp.releaseMs
                                                        * (up.fxSend.hasFxSendReleaseMultiplier ? up.fxSend.fxSendReleaseMultiplier : 0.35f)))
        : fxSendReleaseMsLive;
    const juce::String fxSendReleaseSource = choirMode ? juce::String("choirModeClamp")
        : up.fxSend.hasFxSendReleaseMs ? juce::String("preset.fxSend.fxSendReleaseMs")
        : up.fxSend.hasFxSendReleaseMultiplier ? juce::String("ampReleaseFallback")
        : juce::String("categoryDefault");
    const float ampReleaseMsLive    = up.amp.releaseMs;
    const bool  choirAmpReleaseClamped = choirMode && (up.amp.releaseMs > 900.0f);
    const bool  choirReverbCapApplied  = choirMode && reverbMix >= 0.219f;
    const bool  choirDelayCapApplied   = choirMode && delayMix  >= 0.029f;
    const int   choirActiveVoiceCount  = choirMode ? proc.getSynthEngine().getActiveVoiceCount() : 0;
    const bool  choirNoteDensityFxReduction = choirMode && noteDensityFxOn;
    if (choirMode)
    {
        if (up.amp.releaseMs > 900.0f) warnings.add("CHOIR_RELEASE_TOO_LONG");
        if (reverbMix > 0.22f)         warnings.add("CHOIR_REVERB_TOO_WET");
        if (delayMix  > 0.03f || delayFeedback > 0.08f) warnings.add("CHOIR_DELAY_TOO_HIGH");
        if (choirActiveVoiceCount > 8) warnings.add("CHOIR_TOO_MANY_OVERLAPPING_VOICES");
        if (fxSendReleaseMsLive > 180.0f) warnings.add("CHOIR_FX_SEND_TOO_LONG");
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
        << "dB clampedFields=" << "n/a"
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
        << " choirNoteDensityFxReduction=" << (choirNoteDensityFxReduction ? "true" : "false")
        << " choirActiveVoiceCount=" << choirActiveVoiceCount
        << " choirFxInputAfterNoteOffDb=" << juce::String(fxInDb, 2)
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
    j->setProperty("choirNoteDensityFxReduction", choirNoteDensityFxReduction);
    j->setProperty("choirActiveVoiceCount",     choirActiveVoiceCount);
    j->setProperty("choirFxInputAfterNoteOffDb", fxInDb);
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
            DBG("[DIDITAGAIN preset-quality] latest write FAILED file=" << f.getFullPathName());
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
