#include "PresetMigration.h"
#include "PresetSchema.h"

namespace dida { namespace preset {

bool PresetMigration::isLegacy(const juce::var& json) noexcept
{
    if (! json.isObject()) return false;
    // v2 always sets schemaVersion to "2.0.0".
    const auto sv = json.getProperty("schemaVersion", juce::var()).toString();
    if (sv == kSchemaVersionV2) return false;
    // v1 has presetVersion (int) or no schemaVersion + sampler/oscA fields.
    if (json.hasProperty(key::presetVersion)) return true;
    if (json.hasProperty(key::sampler) || json.hasProperty(key::oscA)) return true;
    return false;
}

static LayerType layerTypeFromString(const juce::String& s)
{
    if (s.equalsIgnoreCase("oscillator")) return LayerType::Oscillator;
    if (s.equalsIgnoreCase("noise"))      return LayerType::Noise;
    if (s.equalsIgnoreCase("texture"))    return LayerType::Texture;
    return LayerType::Sample;
}

static void readEnv(const juce::var& v, AmpEnvV2& env)
{
    if (! v.isObject()) return;
    env.attack  = (float) (double) v.getProperty("attack",  env.attack);
    env.decay   = (float) (double) v.getProperty("decay",   env.decay);
    env.sustain = (float) (double) v.getProperty("sustain", env.sustain);
    env.release = (float) (double) v.getProperty("release", env.release);
}

static void readFilter(const juce::var& v, LayerFilterV2& f, bool& wasPresent)
{
    if (! v.isObject()) { wasPresent = false; return; }
    wasPresent = true;
    f.enabled   = (bool) v.getProperty("enabled", true);
    f.type      = v.getProperty("type", "lowpass").toString();
    f.cutoff    = (float) (double) v.getProperty("cutoff", f.cutoff);
    f.resonance = (float) (double) v.getProperty("resonance", f.resonance);
    f.drive     = (float) (double) v.getProperty("drive", f.drive);
}

static void readLayer(const juce::var& v, LayerV2& L)
{
    if (! v.isObject()) return;
    L.id       = v.getProperty("id", "layer").toString();
    L.name     = v.getProperty("name", "Layer").toString();
    L.type     = layerTypeFromString(v.getProperty("type", "sample").toString());
    L.enabled  = (bool) v.getProperty("enabled", true);
    L.volume   = (float) (double) v.getProperty("volume", 0.8);
    L.pan      = (float) (double) v.getProperty("pan", 0.0);
    readEnv(v.getProperty("ampEnvelope", juce::var()), L.ampEnv);
    readFilter(v.getProperty("filter", juce::var()), L.filter, L.hasFilter);
    if (L.type == LayerType::Sample || L.type == LayerType::Texture)
    {
        L.source        = v.getProperty("source", "").toString();
        L.rootNote      = v.getProperty("rootNote", "C4").toString();
        L.rootMidi      = (int) v.getProperty("rootMidi", 60);
        L.pitchTracking = (bool) v.getProperty("pitchTracking", true);
        L.oneShotMode   = (bool) v.getProperty("oneShotMode", false);
        L.pitchSemis    = (int) v.getProperty("pitch", 0);
        L.fineCents     = (int) v.getProperty("fineTune", 0);
        L.reverse       = (bool) v.getProperty("reverse", false);
        L.loop          = (bool) v.getProperty("loop", false);
        L.cropStart       = (float) (double) v.getProperty("cropStart",       L.cropStart);
        L.cropEnd         = (float) (double) v.getProperty("cropEnd",         L.cropEnd);
        L.loopStart       = (float) (double) v.getProperty("loopStart",       L.loopStart);
        L.loopEnd         = (float) (double) v.getProperty("loopEnd",         L.loopEnd);
        L.loopCrossfadeMs = (float) (double) v.getProperty("loopCrossfadeMs", L.loopCrossfadeMs);
        L.autoLoop        = (bool) v.getProperty("autoLoop",                  L.autoLoop);
    }
    if (L.type == LayerType::Oscillator)
    {
        L.waveform   = v.getProperty("waveform", "sine").toString();
        L.pitchSemis = (int) v.getProperty("pitch", 0);
        L.fineCents  = (int) v.getProperty("fineTune", 0);
    }
}

static void readEffects(const juce::var& v, EffectsV2& fx)
{
    if (! v.isObject()) return;
    auto eq = v.getProperty("eq", juce::var());
    if (eq.isObject()) {
        fx.eqEnabled = (bool) eq.getProperty("enabled", true);
        fx.eqLowCut  = (float) (double) eq.getProperty("lowCut", 80.0);
        fx.eqBody    = (float) (double) eq.getProperty("body", 0.0);
        fx.eqPresence= (float) (double) eq.getProperty("presence", 0.0);
        fx.eqAir     = (float) (double) eq.getProperty("air", 0.0);
    }
    auto sat = v.getProperty("saturation", juce::var());
    if (sat.isObject()) {
        fx.satEnabled = (bool) sat.getProperty("enabled", false);
        fx.satMode    = sat.getProperty("mode", "tape").toString();
        fx.satDrive   = (float) (double) sat.getProperty("drive", 0.1);
        fx.satMix     = (float) (double) sat.getProperty("mix", 0.25);
    }
    auto ch = v.getProperty("chorus", juce::var());
    if (ch.isObject()) {
        fx.chorusEnabled = (bool) ch.getProperty("enabled", false);
        fx.chorusRate    = (float) (double) ch.getProperty("rate", 0.3);
        fx.chorusDepth   = (float) (double) ch.getProperty("depth", 0.2);
        fx.chorusMix     = (float) (double) ch.getProperty("mix", 0.2);
    }
    auto dl = v.getProperty("delay", juce::var());
    if (dl.isObject()) {
        fx.delayEnabled = (bool) dl.getProperty("enabled", false);
        fx.delaySync    = (bool) dl.getProperty("sync", true);
        fx.delayTime    = dl.getProperty("time", "1/4").toString();
        fx.delayFb      = (float) (double) dl.getProperty("feedback", 0.25);
        fx.delayMix     = (float) (double) dl.getProperty("mix", 0.15);
    }
    auto rv = v.getProperty("reverb", juce::var());
    if (rv.isObject()) {
        fx.reverbEnabled = (bool) rv.getProperty("enabled", true);
        fx.reverbSize    = (float) (double) rv.getProperty("size", 0.5);
        fx.reverbDecay   = (float) (double) rv.getProperty("decay", 2.0);
        fx.reverbMix     = (float) (double) rv.getProperty("mix", 0.2);
    }
    auto wd = v.getProperty("width", juce::var());
    if (wd.isObject()) {
        fx.widthEnabled = (bool) wd.getProperty("enabled", true);
        fx.widthAmount  = (float) (double) wd.getProperty("amount", 0.3);
    }
    auto lim = v.getProperty("limiter", juce::var());
    if (lim.isObject()) {
        fx.limiterEnabled = (bool) lim.getProperty("enabled", true);
        fx.limiterCeiling = (float) (double) lim.getProperty("ceiling", -0.5);
    }
}

static HybridPresetV2 readV2(const juce::var& json)
{
    HybridPresetV2 p;
    p.schemaVersion = json.getProperty("schemaVersion", kSchemaVersionV2).toString();
    p.presetId      = json.getProperty("presetId", "").toString();
    p.name          = json.getProperty("name", "Untitled").toString();
    p.bank          = json.getProperty("bank", "User").toString();
    p.category      = json.getProperty("category", "Uncategorized").toString();
    p.subCategory   = json.getProperty("subCategory", "").toString();
    p.author        = json.getProperty("author", "User").toString();
    p.dateCreated   = json.getProperty("dateCreated", "").toString();
    p.dateModified  = json.getProperty("dateModified", "").toString();

    auto readArr = [](const juce::var& v) {
        juce::StringArray out;
        if (auto* a = v.getArray())
            for (auto& x : *a) out.add(x.toString());
        return out;
    };
    p.tags  = readArr(json.getProperty("tags",  juce::var()));
    p.genre = readArr(json.getProperty("genre", juce::var()));
    p.mood  = readArr(json.getProperty("mood",  juce::var()));

    auto si = json.getProperty("sourceImport", juce::var());
    if (si.isObject())
    {
        p.hasSourceImport = true;
        p.sourceOriginalFileName = si.getProperty("originalFileName", "").toString();
        p.sourceSamplePath       = si.getProperty("samplePath", "").toString();
        p.sourceRootNote         = si.getProperty("detectedRootNote", "C4").toString();
        p.sourceRootMidi         = (int) si.getProperty("rootMidi", 60);
        p.sourceRootSrc          = si.getProperty("rootNoteSource", "manual").toString();
        p.sourcePitchTracking    = (bool) si.getProperty("pitchTracking", true);
    }
    auto q = json.getProperty("quality", juce::var());
    if (q.isObject()) {
        p.needsReview      = (bool) q.getProperty("needsReview", false);
        p.rootNoteVerified = (bool) q.getProperty("rootNoteVerified", true);
    }

    if (auto* la = json.getProperty("layers", juce::var()).getArray())
        for (auto& l : *la) { LayerV2 L; readLayer(l, L); p.layers.push_back(L); }

    bool gfPresent = false;
    readFilter(json.getProperty("globalFilter", juce::var()), p.globalFilter, gfPresent);

    readEffects(json.getProperty("effects", juce::var()), p.effects);

    if (auto* ma = json.getProperty("macros", juce::var()).getArray())
    {
        for (auto& m : *ma) {
            if (! m.isObject()) continue;
            MacroV2 M;
            M.id    = m.getProperty("id", "macro").toString();
            M.name  = m.getProperty("name", "Macro").toString();
            M.value = (float) (double) m.getProperty("value", 0.5);
            if (auto* ta = m.getProperty("targets", juce::var()).getArray())
                for (auto& t : *ta) {
                    MacroTargetV2 T;
                    T.path = t.getProperty("path", "").toString();
                    T.min  = (float) (double) t.getProperty("min", 0.0);
                    T.max  = (float) (double) t.getProperty("max", 1.0);
                    M.targets.push_back(T);
                }
            p.macros.push_back(M);
        }
    }
    return p;
}

HybridPresetV2 PresetMigration::toV2(const juce::var& legacyJson)
{
    HybridPresetV2 p;
    p.schemaVersion = kSchemaVersionV2;
    p.name          = legacyJson.getProperty(key::presetName, "Migrated Preset").toString();
    p.author        = legacyJson.getProperty(key::author, "DIDITAGAIN").toString();
    p.bank          = "Factory";
    p.category      = legacyJson.getProperty(key::category, "Uncategorized").toString();
    if (auto* a = legacyJson.getProperty(key::tags, juce::var()).getArray())
        for (auto& t : *a) p.tags.add(t.toString());

    // Layer 1 — sample (if legacy preset references a sampler.instrument).
    LayerV2 L;
    L.id = "layer_1"; L.name = "Main Sample";
    L.type = LayerType::Sample;
    L.enabled = true; L.volume = 0.85f; L.pan = 0.0f;
    auto sampler = legacyJson.getProperty(key::sampler, juce::var());
    if (sampler.isObject()) {
        // Engine resolves multisample folder names by joining with Samples/.
        L.source = "Samples/" + sampler.getProperty("instrument", "").toString();
        L.rootNote = "C4"; L.rootMidi = 60;
    } else {
        L.enabled = false; // pure-synth legacy preset
    }
    auto env1 = legacyJson.getProperty(key::env1, juce::var());
    if (env1.isObject()) readEnv(env1, L.ampEnv);
    auto f1 = legacyJson.getProperty(key::filter1, juce::var());
    if (f1.isObject()) {
        L.hasFilter = true;
        L.filter.enabled = true;
        L.filter.type = "lowpass";
        L.filter.cutoff   = (float) (double) f1.getProperty("cutoff", 9000.0);
        L.filter.resonance= (float) (double) f1.getProperty("resonance", 0.15);
        L.filter.drive    = (float) (double) f1.getProperty("drive", 0.0);
    }
    p.layers.push_back(L);

    // Layer 2 — oscillator stub from oscA so non-sample legacy presets
    // still produce an audible body when loaded by the v2 path.
    auto oscA = legacyJson.getProperty(key::oscA, juce::var());
    if (oscA.isObject()) {
        LayerV2 O;
        O.id = "layer_2"; O.name = "Osc A";
        O.type = LayerType::Oscillator;
        O.enabled = (! sampler.isObject());
        O.waveform = oscA.getProperty("waveform", "saw").toString();
        O.volume = (float) (double) oscA.getProperty("level", 0.6);
        O.ampEnv = L.ampEnv;
        p.layers.push_back(O);
    }

    // FX
    auto fx = legacyJson.getProperty(key::fxChain, juce::var());
    if (fx.isObject()) {
        p.effects.chorusMix    = (float) (double) fx.getProperty("chorusMix", 0.0);
        p.effects.chorusEnabled= p.effects.chorusMix > 0.001f;
        p.effects.delayMix     = (float) (double) fx.getProperty("delayMix", 0.0);
        p.effects.delayEnabled = p.effects.delayMix > 0.001f;
        p.effects.delayFb      = (float) (double) fx.getProperty("delayFeedback", 0.25);
        p.effects.reverbMix    = (float) (double) fx.getProperty("reverbMix", 0.0);
        p.effects.reverbSize   = (float) (double) fx.getProperty("reverbSize", 0.5);
        p.effects.reverbEnabled= p.effects.reverbMix > 0.001f;
        p.effects.satDrive     = (float) (double) fx.getProperty("distortionAmount", 0.0);
        p.effects.satEnabled   = p.effects.satDrive > 0.001f;
    }
    return p;
}

bool PresetMigration::parseAny(const juce::var& json, HybridPresetV2& outPreset)
{
    if (! json.isObject()) return false;
    if (isLegacy(json)) {
        outPreset = toV2(json);
        return true;
    }
    outPreset = readV2(json);
    return true;
}

}} // namespace dida::preset
