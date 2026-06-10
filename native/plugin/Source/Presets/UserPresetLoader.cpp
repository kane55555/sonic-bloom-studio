//==============================================================================
//  UserPresetLoader.cpp
//
//  ----------------------------------------------------------------------------
//  .diapreset JSON schema (v1)
//  ----------------------------------------------------------------------------
//  {
//    "schemaVersion": 1,
//    "presetName":    "Dark Pain Guitar",
//    "category":      "Guitars",
//    "sourceInstrument": {
//      "type": "multisampleFolder",
//      "path": "C:/.../Samples/Guitars/Guitar 1",
//      "mappingMode": "hardZones",
//      "rootNotePattern": ["C","D#","F#","A"]
//    },
//    "amp":      { gainDb, pan, attackMs, decayMs, sustain, releaseMs },
//    "filter":   { enabled, type, cutoffHz, resonance, drive, keytrack },
//    "layers":   { "main": {...}, "layer2": {...} },
//    "fx": {
//      "chorus":     { enabled, rateHz, depth, mix },
//      "delay":      { enabled, timeMs, feedback, mix },
//      "reverb":     { enabled, size, damping, mix },
//      "saturation": { enabled, drive, mix }
//    },
//    "modulation": { "lfo1": {...}, "lfo2": {...} },
//    "advanced":   { sampleStartMs, randomStartMs, velocityToGain,
//                    velocityToCutoff, humanizePitchCents,
//                    humanizeTimingMs, polyphony }
//  }
//==============================================================================
#include "UserPresetLoader.h"
#include "../DSP/SampleLibrary.h"
#include "HybridPresetApplier.h"
#include "../PluginProcessor.h"
#include "../DSP/SynthEngine.h"
#include "../DSP/Voice.h"
#include "../DSP/Engines/IEngineSource.h"
#include "../DSP/Engines/AnalogEngine.h"
#include "../DSP/Engines/SupersawEngine.h"
#include "../DSP/Engines/FmEngine.h"
#include "../DSP/Engines/WavetableEngine.h"
#include "../DSP/Engines/GranularEngine.h"
#include "../DSP/Engines/PcmEngine.h"
#include "../DSP/Engines/NeuralTextureEngine.h"
#include <algorithm>
#include <map>


namespace dida { namespace userpreset {

static void didaUserPresetLog(const juce::String& msg)
{
    juce::Logger::writeToLog("[DIDITAGAIN user-preset] " + msg);
}

static float getF(const juce::var& obj, const char* key, float fallback)
{
    if (! obj.isObject() || ! obj.hasProperty(key)) return fallback;
    return static_cast<float>((double) obj.getProperty(key, fallback));
}
static int getI(const juce::var& obj, const char* key, int fallback)
{
    if (! obj.isObject() || ! obj.hasProperty(key)) return fallback;
    return static_cast<int>((int) obj.getProperty(key, fallback));
}
static bool getB(const juce::var& obj, const char* key, bool fallback)
{
    if (! obj.isObject() || ! obj.hasProperty(key)) return fallback;
    return (bool) obj.getProperty(key, fallback);
}
static juce::String getS(const juce::var& obj, const char* key, const juce::String& fallback)
{
    if (! obj.isObject() || ! obj.hasProperty(key)) return fallback;
    return obj.getProperty(key, fallback).toString();
}

static LayerBlock parseLayer(const juce::var& v, const LayerBlock& def)
{
    if (! v.isObject()) return def;
    LayerBlock l = def;
    l.enabled     = getB(v, "enabled",     def.enabled);
    l.gainDb      = getF(v, "gainDb",      def.gainDb);
    l.pan         = getF(v, "pan",         def.pan);
    l.octave      = getI(v, "octave",      def.octave);
    l.semitone    = getI(v, "semitone",    def.semitone);
    l.detuneCents = getF(v, "detuneCents", def.detuneCents);
    l.blendMode          = getS(v, "blendMode",          def.blendMode);
    l.eqRole             = getS(v, "eqRole",             def.eqRole);
    l.followMainEnvelope = getB(v, "followMainEnvelope", def.followMainEnvelope);
    l.maxGainDb          = getF(v, "maxGainDb",          def.maxGainDb);
    return l;
}

bool parseFile(const juce::File& file, UserPreset& out, juce::String& errorOut)
{
    if (! file.existsAsFile())
    {
        errorOut = "file not found: " + file.getFullPathName();
        return false;
    }

    auto json = juce::JSON::parse(file);
    if (! json.isObject())
    {
        errorOut = "invalid JSON";
        return false;
    }

    out.schemaVersion = getI(json, "schemaVersion", 0);
    if (out.schemaVersion < 1)
    {
        errorOut = "missing or unsupported schemaVersion";
        return false;
    }

    // "presetName" is canonical; AI Texture demo-pack files use "name". Accept
    // either so packs authored against the documented public schema still load.
    out.presetName = getS(json, "presetName", getS(json, "name", {}));
    if (out.presetName.isEmpty())
    {
        errorOut = "missing presetName";
        return false;
    }
    out.category = getS(json, "category", "User");

    // Source resolution accepts two shapes:
    //   1) "sourceInstrument": { type, path, mappingMode, rootNotePattern }
    //   2) "samples":          { rootFolder, required, fallbackSynthIfMissing }
    // (2) is used by self-contained AI Texture presets whose audible content is
    // the cached neural texture partial; the multisample folder is optional.
    auto src = json.getProperty("sourceInstrument", juce::var());
    auto samples = json.getProperty("samples", juce::var());
    if (src.isObject())
    {
        out.source.type        = getS(src, "type", "multisampleFolder");
        out.source.path        = getS(src, "path", {});
        out.source.mappingMode = getS(src, "mappingMode", "hardZones");
        out.source.rootNotePattern.clear();
        if (auto* arr = src.getProperty("rootNotePattern", juce::var()).getArray())
            for (auto& v : *arr) out.source.rootNotePattern.add(v.toString());
        if (out.source.path.isEmpty())
        {
            errorOut = "missing sourceInstrument.path";
            return false;
        }
    }
    else if (samples.isObject())
    {
        out.source.type        = "multisampleFolder";
        out.source.path        = getS(samples, "rootFolder", {});
        out.source.mappingMode = "nearest";
        out.sourceRequired         = getB(samples, "required", false);
        out.fallbackSynthIfMissing = getB(samples, "fallbackSynthIfMissing", true);
        // An optional/self-contained source may legitimately be empty.
        if (out.source.path.isEmpty() && out.sourceRequired)
        {
            errorOut = "missing samples.rootFolder";
            return false;
        }
    }
    else
    {
        // Neither block present. Only valid when the preset is fully partial-
        // driven (e.g. a neural-texture-only demo). Otherwise reject.
        const bool hasPartials = json.getProperty("partials", juce::var()).isArray();
        if (! hasPartials)
        {
            errorOut = "missing sourceInstrument";
            return false;
        }
        out.sourceRequired = false;
        out.fallbackSynthIfMissing = true;
    }

    // "amp" is canonical; AI Texture demo-pack files use "ampEnvelope".
    auto amp = json.getProperty("amp", juce::var());
    if (! amp.isObject()) amp = json.getProperty("ampEnvelope", juce::var());
    out.amp.gainDb    = getF(amp, "gainDb",    out.amp.gainDb);
    out.amp.pan       = getF(amp, "pan",       out.amp.pan);
    out.amp.attackMs  = getF(amp, "attackMs",  out.amp.attackMs);
    out.amp.decayMs   = getF(amp, "decayMs",   out.amp.decayMs);
    out.amp.sustain   = getF(amp, "sustain",   out.amp.sustain);
    out.amp.releaseMs = getF(amp, "releaseMs", out.amp.releaseMs);

    auto flt = json.getProperty("filter", juce::var());
    out.filter.enabled   = getB(flt, "enabled",   out.filter.enabled);
    out.filter.type      = getS(flt, "type",      out.filter.type);
    out.filter.cutoffHz  = getF(flt, "cutoffHz",  out.filter.cutoffHz);
    out.filter.resonance = getF(flt, "resonance", out.filter.resonance);
    out.filter.drive     = getF(flt, "drive",     out.filter.drive);
    // accept both "keytrack" and the demo-pack "keyTrack" spelling.
    out.filter.keytrack  = getF(flt, "keytrack",  getF(flt, "keyTrack", out.filter.keytrack));

    auto layers = json.getProperty("layers", juce::var());
    out.main   = parseLayer(layers.getProperty("main",   juce::var()), out.main);
    out.layer2 = parseLayer(layers.getProperty("layer2", juce::var()), out.layer2);

    auto fx = json.getProperty("fx", juce::var());
    auto cho = fx.getProperty("chorus", juce::var());
    out.chorus.enabled = getB(cho, "enabled", false);
    out.chorus.rateHz  = getF(cho, "rateHz", out.chorus.rateHz);
    out.chorus.depth   = getF(cho, "depth",  out.chorus.depth);
    out.chorus.mix     = getF(cho, "mix",    out.chorus.mix);

    auto del = fx.getProperty("delay", juce::var());
    out.delay.enabled  = getB(del, "enabled", false);
    out.delay.timeMs   = getF(del, "timeMs",   out.delay.timeMs);
    out.delay.hasFeedback = del.isObject() && (del.hasProperty("feedback") || del.hasProperty("delayFeedback"));
    out.delay.feedback = getF(del, "feedback", getF(del, "delayFeedback", out.delay.feedback));
    out.delay.mix      = getF(del, "mix",      out.delay.mix);

    auto rev = fx.getProperty("reverb", juce::var());
    out.reverb.enabled = getB(rev, "enabled", false);
    out.reverb.size    = getF(rev, "size",    out.reverb.size);
    out.reverb.damping = getF(rev, "damping", out.reverb.damping);
    out.reverb.hasMix  = rev.isObject() && (rev.hasProperty("mix") || rev.hasProperty("reverbMix") || rev.hasProperty("wet"));
    out.reverb.mix     = getF(rev, "mix", getF(rev, "reverbMix", getF(rev, "wet", out.reverb.mix)));
    out.reverb.bypass  = getB(rev, "bypass", false);
    if (rev.isObject() && rev.hasProperty("preDelayMs")) out.reverb.preDelayMs = getF(rev, "preDelayMs", -1.0f);
    if (rev.isObject() && (rev.hasProperty("duckingEnabled") || rev.hasProperty("duckingAmount")))
    {
        out.reverb.hasDucking     = true;
        out.reverb.duckingEnabled = getB(rev, "duckingEnabled", true);
        out.reverb.duckingAmount  = getF(rev, "duckingAmount", -1.0f);
    }
    if (rev.isObject() && rev.hasProperty("inputHighpassHz")) out.reverb.inputHighpassHz = getF(rev, "inputHighpassHz", -1.0f);
    if (rev.isObject() && rev.hasProperty("inputLowpassHz"))  out.reverb.inputLowpassHz  = getF(rev, "inputLowpassHz", -1.0f);

    auto sat = fx.getProperty("saturation", juce::var());
    out.saturation.enabled = getB(sat, "enabled", false);
    out.saturation.drive   = getF(sat, "drive",   out.saturation.drive);
    out.saturation.mix     = getF(sat, "mix",     out.saturation.mix);

    auto fxSend = json.getProperty("fxSend", juce::var());
    out.fxSend.hasFxSendReleaseMs = fxSend.isObject() && fxSend.hasProperty("fxSendReleaseMs");
    out.fxSend.fxSendReleaseMs = getF(fxSend, "fxSendReleaseMs", out.fxSend.fxSendReleaseMs);
    out.fxSend.hasFxSendMaximumReleaseMs = fxSend.isObject() && fxSend.hasProperty("fxSendMaximumReleaseMs");
    out.fxSend.fxSendMaximumReleaseMs = getF(fxSend, "fxSendMaximumReleaseMs", out.fxSend.fxSendMaximumReleaseMs);
    out.fxSend.hasFxSendReleaseMultiplier = fxSend.isObject() && fxSend.hasProperty("fxSendReleaseMultiplier");
    out.fxSend.fxSendReleaseMultiplier = getF(fxSend, "fxSendReleaseMultiplier", out.fxSend.fxSendReleaseMultiplier);
    out.fxSend.noteOffStopsFxSend = getB(fxSend, "noteOffStopsFxSend", out.fxSend.noteOffStopsFxSend);

    out.choirMode = getB(json, "choirMode", out.choirMode);
    auto safety = json.getProperty("safety", juce::var());
    out.safety.hasChoirFxSendReleaseMaxMs = safety.isObject() && safety.hasProperty("choirFxSendReleaseMaxMs");
    out.safety.choirFxSendReleaseMaxMs = getF(safety, "choirFxSendReleaseMaxMs", out.safety.choirFxSendReleaseMaxMs);

    auto mod = json.getProperty("modulation", juce::var());
    auto parseLfo = [](const juce::var& v, LfoBlock& l) {
        if (! v.isObject()) return;
        l.enabled = getB(v, "enabled", false);
        l.target  = getS(v, "target", {});
        l.shape   = getS(v, "shape", "sine");
        l.rateHz  = getF(v, "rateHz", l.rateHz);
        l.depth   = getF(v, "depth",  l.depth);
    };
    parseLfo(mod.getProperty("lfo1", juce::var()), out.lfo1);
    parseLfo(mod.getProperty("lfo2", juce::var()), out.lfo2);

    auto adv = json.getProperty("advanced", juce::var());
    out.advanced.sampleStartMs      = getF(adv, "sampleStartMs",      out.advanced.sampleStartMs);
    out.advanced.randomStartMs      = getF(adv, "randomStartMs",      out.advanced.randomStartMs);
    out.advanced.velocityToGain     = getF(adv, "velocityToGain",     out.advanced.velocityToGain);
    out.advanced.velocityToCutoff   = getF(adv, "velocityToCutoff",   out.advanced.velocityToCutoff);
    out.advanced.humanizePitchCents = getF(adv, "humanizePitchCents", out.advanced.humanizePitchCents);
    out.advanced.humanizeTimingMs   = getF(adv, "humanizeTimingMs",   out.advanced.humanizeTimingMs);
    out.advanced.polyphony          = getI(adv, "polyphony",          out.advanced.polyphony);

    // ---- v2 optional blocks --------------------------------------------------
    auto mac = json.getProperty("macros", juce::var());
    out.macros.tone      = getF(mac, "tone",      out.macros.tone);
    out.macros.movement  = getF(mac, "movement",  out.macros.movement);
    out.macros.width     = getF(mac, "width",     out.macros.width);
    out.macros.warmth    = getF(mac, "warmth",    out.macros.warmth);
    out.macros.attack    = getF(mac, "attack",    out.macros.attack);
    out.macros.release   = getF(mac, "release",   out.macros.release);
    out.macros.space     = getF(mac, "space",     out.macros.space);
    out.macros.character = getF(mac, "character", out.macros.character);

    auto vel = json.getProperty("velocity", juce::var());
    out.velocity.toGain       = getF(vel, "toGain",       out.velocity.toGain);
    out.velocity.toCutoff     = getF(vel, "toCutoff",     out.velocity.toCutoff);
    out.velocity.toAttack     = getF(vel, "toAttack",     out.velocity.toAttack);
    out.velocity.toLayerBlend = getF(vel, "toLayerBlend", out.velocity.toLayerBlend);

    auto leq = json.getProperty("layerEq", juce::var());
    out.layerEq.mainBodyHz   = getF(leq, "mainBodyHz",   out.layerEq.mainBodyHz);
    out.layerEq.mainAirHz    = getF(leq, "mainAirHz",    out.layerEq.mainAirHz);
    out.layerEq.layer2BodyHz = getF(leq, "layer2BodyHz", out.layerEq.layer2BodyHz);
    out.layerEq.layer2AirHz  = getF(leq, "layer2AirHz",  out.layerEq.layer2AirHz);

    auto fm = json.getProperty("filterMovement", juce::var());
    out.filterMovement.enabled = getB(fm, "enabled", out.filterMovement.enabled);
    out.filterMovement.depth   = getF(fm, "depth",   out.filterMovement.depth);
    out.filterMovement.rateHz  = getF(fm, "rateHz",  out.filterMovement.rateHz);

    out.experimental = getB(json, "experimental", false);

    // Mod-matrix routings (optional). Unknown source/dest names are kept
    // verbatim so round-trip save preserves user intent.
    out.modMatrix.clearQuick();
    auto mm = json.getProperty("modMatrix", juce::var());
    if (mm.isArray())
    {
        for (auto& v : *mm.getArray())
        {
            ModMatrixEntry e;
            e.source  = v.getProperty("source",  "").toString();
            e.dest    = v.getProperty("dest",    "").toString();
            e.amount  = (float) (double) v.getProperty("amount",  0.0);
            e.bipolar = (bool)         v.getProperty("bipolar", true);
            if (e.source.isNotEmpty() && e.dest.isNotEmpty())
                out.modMatrix.add(e);
        }
    }

    // ------- Multi-engine partials (v2 additive) -------
    // Absent => leave out.partials empty and let the engine run the legacy
    // PCM/multisample path exactly as before. We never fail parse on a bad
    // partial entry: invalid ones are dropped with a log warning.
    out.engineType = getS(json, "engineType", {});
    out.partials.clearQuick();
    auto parts = json.getProperty("partials", juce::var());
    if (parts.isArray())
    {
        for (auto& v : *parts.getArray())
        {
            if (! v.isObject()) continue;
            if (out.partials.size() >= 4) break;
            UserPreset::PartialBlock pb;
            pb.enabled    = getB(v, "enabled",    pb.enabled);
            pb.engineType = getS(v, "engineType", pb.engineType);
            pb.level      = getF(v, "level",      pb.level);
            pb.pan        = getF(v, "pan",        pb.pan);
            pb.pitchSemis = getI(v, "pitchSemis", pb.pitchSemis);
            pb.fineCents  = getF(v, "fineCents",  pb.fineCents);
            pb.blendMode          = getS(v, "blendMode",          pb.blendMode);
            pb.eqRole             = getS(v, "eqRole",             pb.eqRole);
            pb.followMainEnvelope = getB(v, "followMainEnvelope", pb.followMainEnvelope);
            pb.maxGainDb          = getF(v, "maxGainDb",          pb.maxGainDb);
            // Demo-pack additive: top-level levelDb + isNeuralTexture hint.
            if (v.isObject() && v.hasProperty("levelDb"))
            {
                pb.hasLevelDb = true;
                pb.levelDb    = getF(v, "levelDb", pb.levelDb);
            }
            pb.isNeuralTexture = getB(v, "isNeuralTexture", pb.isNeuralTexture);
            pb.engineParams = v.getProperty("engineParams", juce::var());

            auto pAmp = v.getProperty("amp", juce::var());
            pb.amp.gainDb    = getF(pAmp, "gainDb",    pb.amp.gainDb);
            pb.amp.pan       = getF(pAmp, "pan",       pb.amp.pan);
            pb.amp.attackMs  = getF(pAmp, "attackMs",  pb.amp.attackMs);
            pb.amp.decayMs   = getF(pAmp, "decayMs",   pb.amp.decayMs);
            pb.amp.sustain   = getF(pAmp, "sustain",   pb.amp.sustain);
            pb.amp.releaseMs = getF(pAmp, "releaseMs", pb.amp.releaseMs);

            auto pFlt = v.getProperty("filter", juce::var());
            pb.filter.enabled   = getB(pFlt, "enabled",   pb.filter.enabled);
            pb.filter.type      = getS(pFlt, "type",      pb.filter.type);
            pb.filter.cutoffHz  = getF(pFlt, "cutoffHz",  pb.filter.cutoffHz);
            pb.filter.resonance = getF(pFlt, "resonance", pb.filter.resonance);
            pb.filter.drive     = getF(pFlt, "drive",     pb.filter.drive);
            pb.filter.keytrack  = getF(pFlt, "keytrack",  pb.filter.keytrack);

            auto pLfo = v.getProperty("lfo", juce::var());
            if (pLfo.isObject())
            {
                pb.lfo.enabled = getB(pLfo, "enabled", pb.lfo.enabled);
                pb.lfo.target  = getS(pLfo, "target",  pb.lfo.target);
                pb.lfo.shape   = getS(pLfo, "shape",   pb.lfo.shape);
                pb.lfo.rateHz  = getF(pLfo, "rateHz",  pb.lfo.rateHz);
                pb.lfo.depth   = getF(pLfo, "depth",   pb.lfo.depth);
            }

            auto pMods = v.getProperty("mods", juce::var());
            if (pMods.isArray())
            {
                for (auto& mv : *pMods.getArray())
                {
                    ModMatrixEntry e;
                    e.source  = mv.getProperty("source",  "").toString();
                    e.dest    = mv.getProperty("dest",    "").toString();
                    e.amount  = (float) (double) mv.getProperty("amount",  0.0);
                    e.bipolar = (bool)         mv.getProperty("bipolar", true);
                    if (e.source.isNotEmpty() && e.dest.isNotEmpty())
                        pb.mods.add(e);
                }
            }

            out.partials.add(pb);
        }
    }

    // ------- AI Texture v0.1 (cached) metadata -------
    // Optional. A MISSING "ai" object leaves out.ai defaulted to disabled so
    // every pre-AI .diapreset behaves exactly as before.
    auto ai = json.getProperty("ai", juce::var());
    if (ai.isObject())
    {
        out.ai.present        = true;
        out.ai.enabled        = getB(ai, "enabled",        out.ai.enabled);
        out.ai.profileVersion = getI(ai, "profileVersion", out.ai.profileVersion);
        out.ai.provider       = getS(ai, "provider",       out.ai.provider);
        out.ai.analysisFile   = getS(ai, "analysisFile",   out.ai.analysisFile);
        out.ai.textureMode    = getS(ai, "textureMode",    out.ai.textureMode);

        auto tp = ai.getProperty("timbreProfile", juce::var());
        if (tp.isObject())
        {
            out.ai.timbreProfile.brightness       = getF(tp, "brightness",       out.ai.timbreProfile.brightness);
            out.ai.timbreProfile.harmonicDensity  = getF(tp, "harmonicDensity",  out.ai.timbreProfile.harmonicDensity);
            out.ai.timbreProfile.noiseAir         = getF(tp, "noiseAir",         out.ai.timbreProfile.noiseAir);
            out.ai.timbreProfile.attackNoise      = getF(tp, "attackNoise",      out.ai.timbreProfile.attackNoise);
            out.ai.timbreProfile.pitchInstability = getF(tp, "pitchInstability", out.ai.timbreProfile.pitchInstability);
            out.ai.timbreProfile.bodyWarmth       = getF(tp, "bodyWarmth",       out.ai.timbreProfile.bodyWarmth);
        }
    }

    return true;
}

static bool folderHasWavs(const juce::File& folder)
{
    return folder.isDirectory()
        && ! folder.findChildFiles(juce::File::findFiles, true, "*.wav").isEmpty();
}

static void addCandidate(juce::Array<juce::File>& candidates, const juce::File& file)
{
    const auto path = file.getFullPathName();
    if (path.isEmpty()) return;

    for (auto& existing : candidates)
        if (existing.getFullPathName().equalsIgnoreCase(path))
            return;

    candidates.add(file);
}

static juce::File findMultisampleFolderByName(const juce::String& folderName,
                                              const juce::String& preferredParent)
{
    if (folderName.isEmpty()) return {};

    auto root = dida::SampleLibrary::getSamplesRoot();
    if (! root.isDirectory()) return {};

    auto dirs = root.findChildFiles(juce::File::findDirectories, true);
    std::sort(dirs.begin(), dirs.end(), [](const juce::File& a, const juce::File& b)
    {
        return a.getFullPathName().length() < b.getFullPathName().length();
    });

    auto parentMatches = [&](const juce::File& dir)
    {
        if (preferredParent.isEmpty()) return true;
        const auto parent = dir.getParentDirectory().getFileName();
        return parent.equalsIgnoreCase(preferredParent)
            || (preferredParent.endsWithIgnoreCase("s") && parent.equalsIgnoreCase(preferredParent.dropLastCharacters(1)))
            || (! preferredParent.endsWithIgnoreCase("s") && parent.equalsIgnoreCase(preferredParent + "s"));
    };

    for (auto& dir : dirs)
    {
        const auto normalised = dir.getFullPathName().replaceCharacter('\\', '/');
        if (normalised.containsIgnoreCase("/Samples/Presets/")) continue;
        if (! dir.getFileName().equalsIgnoreCase(folderName)) continue;
        if (parentMatches(dir) && folderHasWavs(dir)) return dir;
    }

    for (auto& dir : dirs)
    {
        const auto normalised = dir.getFullPathName().replaceCharacter('\\', '/');
        if (normalised.containsIgnoreCase("/Samples/Presets/")) continue;
        if (dir.getFileName().equalsIgnoreCase(folderName) && folderHasWavs(dir)) return dir;
    }

    return {};
}

juce::File resolveSourcePath(const juce::String& rawPath)
{
    auto src = rawPath.replaceCharacter('\\', '/').trim();
    while ((src.startsWithChar('"') && src.endsWithChar('"'))
        || (src.startsWithChar('\'') && src.endsWithChar('\'')))
        src = src.substring(1, src.length() - 1).trim();
    while (src.endsWithChar('/') && src.length() > 1)
        src = src.dropLastCharacters(1);
    if (src.isEmpty()) return {};

    auto samplesRoot = dida::SampleLibrary::getSamplesRoot();
    auto docsRoot = samplesRoot.getParentDirectory();
    juce::Array<juce::File> candidates;

    auto expanded = src;
    const auto docsPath = docsRoot.getFullPathName().replaceCharacter('\\', '/');
    // {DIDA_DOCS} is the portable token preferred by AI Texture Import v0.2 — it
    // expands to the user's "Documents/DIDITAGAIN STUDIO" root so imported
    // texture paths survive moving the project between machines.
    expanded = expanded.replace("{DIDA_DOCS}", docsPath, true);
    expanded = expanded.replace("{DocsRoot}", docsPath, true);
    expanded = expanded.replace("{Docs}",     docsPath, true);
    expanded = expanded.replace("{Documents}", docsPath, true);
    expanded = expanded.replace("{Samples}",  samplesRoot.getFullPathName().replaceCharacter('\\', '/'), true);

    if (juce::File::isAbsolutePath(expanded))
    {
        juce::File absFile (expanded);
        // Honour an absolute sourceInstrument.path EXACTLY when it resolves
        // to a real directory. Do not reinterpret it under Presets/User —
        // base instrument samples live in Samples/<Category>/<Instrument>/.
        if (absFile.isDirectory())
            return absFile;
        // AI Texture v0.2: a cached neural texture is a single WAV *file*, not a
        // folder. The folder-oriented candidate search below (folderHasWavs)
        // would otherwise discard a valid absolute file path and fall through to
        // a category fallback, which is what produced AI_TEXTURE_MISSING_FILE.
        // Return the file directly when it exists on disk.
        if (absFile.existsAsFile())
            return absFile;
        addCandidate(candidates, absFile);
    }

    if (expanded.startsWithIgnoreCase("Samples/"))
        addCandidate(candidates, samplesRoot.getChildFile(expanded.substring(8)));

    addCandidate(candidates, docsRoot.getChildFile(expanded));
    addCandidate(candidates, samplesRoot.getChildFile(expanded));

    juce::String samplesRelative;
    const int samplesMarker = expanded.indexOfIgnoreCase("/Samples/");
    if (samplesMarker >= 0)
        samplesRelative = expanded.substring(samplesMarker + 9);
    else if (expanded.startsWithIgnoreCase("Samples/"))
        samplesRelative = expanded.substring(8);

    if (samplesRelative.isNotEmpty())
    {
        addCandidate(candidates, samplesRoot.getChildFile(samplesRelative));
        const int slash = samplesRelative.indexOfChar('/');
        if (slash > 0)
        {
            const auto category = samplesRelative.substring(0, slash);
            const auto rest = samplesRelative.substring(slash + 1);
            if (category.endsWithIgnoreCase("s"))
                addCandidate(candidates, samplesRoot.getChildFile(category.dropLastCharacters(1)).getChildFile(rest));
            else
                addCandidate(candidates, samplesRoot.getChildFile(category + "s").getChildFile(rest));
        }
    }

    // A directly-resolved file (e.g. a cached neural texture WAV) wins first.
    for (auto& candidate : candidates)
        if (candidate.existsAsFile())
            return candidate;

    for (auto& candidate : candidates)
        if (folderHasWavs(candidate))
            return candidate;

    juce::String preferredParent;
    juce::String folderName;
    auto rel = samplesRelative.isNotEmpty() ? samplesRelative : expanded;
    const int lastSlash = rel.lastIndexOfChar('/');
    folderName = lastSlash >= 0 ? rel.substring(lastSlash + 1) : juce::File(rel).getFileName();
    const int firstSlash = rel.indexOfChar('/');
    if (firstSlash > 0) preferredParent = rel.substring(0, firstSlash);

    auto discovered = findMultisampleFolderByName(folderName, preferredParent);
    if (discovered.isDirectory())
    {
        didaUserPresetLog("resolved missing source " + rawPath + " -> " + discovered.getFullPathName());
        return discovered;
    }

    // Final fallback: if the preset's category folder (e.g. Samples/Synths/)
    // exists, use ANY mapped sub-instrument the user dropped in there. This is
    // what lets a user drop "Samples/Synths/My Juno/" and have every vintage
    // synth preset automatically play it instead of falling through to the
    // silent factory-synth path.
    auto tryCategoryFolder = [&](const juce::String& name) -> juce::File
    {
        if (name.isEmpty()) return {};
        auto dir = samplesRoot.getChildFile(name);
        if (! dir.isDirectory()) return {};

        auto subdirs = dir.findChildFiles(juce::File::findDirectories, false);
        std::sort(subdirs.begin(), subdirs.end(), [](const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compareNatural(b.getFileName()) < 0;
        });
        for (auto& sub : subdirs)
            if (folderHasWavs(sub))
                return sub;

        if (folderHasWavs(dir))
            return dir;

        return {};
    };

    if (preferredParent.isNotEmpty())
    {
        for (auto& variant : { preferredParent,
                               preferredParent.endsWithIgnoreCase("s")
                                   ? preferredParent.dropLastCharacters(1)
                                   : preferredParent + "s" })
        {
            auto picked = tryCategoryFolder(variant);
            if (picked.isDirectory())
            {
                didaUserPresetLog("resolved missing source " + rawPath
                    + " -> category fallback " + picked.getFullPathName());
                return picked;
            }
        }
    }

    return candidates.isEmpty() ? juce::File(expanded) : candidates.getFirst();
}

// ---- APVTS setters (mirror PresetManager.cpp helpers) ---------------------
static void setParamRaw(juce::AudioProcessorParameter* p, float normalised)
{
    if (p != nullptr)
        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalised));
}

static void setParamById(juce::AudioProcessor& proc, const char* id, float value)
{
    for (auto* param : proc.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            if (withId->paramID == id)
            {
                if (auto* fp = dynamic_cast<juce::RangedAudioParameter*>(withId))
                    setParamRaw(fp, fp->convertTo0to1(value));
                return;
            }
}

static void setChoiceById(juce::AudioProcessor& proc, const char* id, int index)
{
    for (auto* param : proc.getParameters())
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(param))
            if (c->paramID == id)
            {
                setParamRaw(c, c->convertTo0to1(static_cast<float>(index)));
                return;
            }
}

static int filterTypeIndex(const juce::String& type)
{
    if (type.equalsIgnoreCase("lowpass") || type.equalsIgnoreCase("lp"))   return 1; // LP24
    if (type.equalsIgnoreCase("highpass")|| type.equalsIgnoreCase("hp"))   return 3; // HP24
    if (type.equalsIgnoreCase("bandpass")|| type.equalsIgnoreCase("bp"))   return 4;
    if (type.equalsIgnoreCase("notch"))                                    return 5;
    return 1;
}

static int lfoShapeIndex(const juce::String& s)
{
    if (s.equalsIgnoreCase("sine"))     return 0;
    if (s.equalsIgnoreCase("triangle")) return 1;
    if (s.equalsIgnoreCase("saw"))      return 2;
    if (s.equalsIgnoreCase("square"))   return 3;
    if (s.equalsIgnoreCase("snh") || s.equalsIgnoreCase("s&h")) return 4;
    return 0;
}

// ---- Category-aware tuning (the heart of "make presets sound expensive") ----

namespace {

enum class Family {
    PianoKeys, Lead, Pad, ChoirVox, Brass, Guitar, Bell, Pluck, Bass808, FxRiser,
    Synth,   // vintage analog modeling presets — strict safety caps
    Other
};

Family familyOf(const juce::String& categoryIn)
{
    const auto c = categoryIn.toLowerCase();
    if (c.contains("piano") || c.contains("keys") || c == "painpianos") return Family::PianoKeys;
    if (c.contains("lead")  || c == "alienleads")                       return Family::Lead;
    if (c.contains("pad")   || c == "darkpads")                         return Family::Pad;
    if (c.contains("choir") || c.contains("vox") || c.contains("vocal")) return Family::ChoirVox;
    if (c.contains("brass") || c.contains("trumpet") || c.contains("horn")) return Family::Brass;
    if (c.contains("guitar"))                                            return Family::Guitar;
    if (c.contains("bell"))                                              return Family::Bell;
    if (c.contains("pluck"))                                             return Family::Pluck;
    if (c.contains("808")   || c.contains("bass") || c.contains("sub")) return Family::Bass808;
    if (c.contains("fx")    || c.contains("riser"))                     return Family::FxRiser;
    if (c == "synth" || c.contains("vintage"))                          return Family::Synth;
    return Family::Other;
}

// Allowed parent-folder hints under Samples/ for each family. Used for
// non-fatal validation warnings on .diapreset loads.
juce::StringArray expectedParentsFor(Family f)
{
    switch (f) {
        case Family::PianoKeys: return { "Pianos", "Piano", "Keys" };
        case Family::Lead:      return { "Synths", "Synth", "Leads" };
        case Family::Pad:       return { "Synths", "Pads",  "Choirs" };
        case Family::ChoirVox:  return { "Choirs", "Vox",   "Vocals" };
        case Family::Brass:     return { "Brass" };
        case Family::Guitar:    return { "Guitars", "Guitar" };
        case Family::Bell:      return { "Bells" };
        case Family::Pluck:     return { "Plucks", "Synths" };
        case Family::Bass808:   return { "808", "Bass", "Subs" };
        case Family::FxRiser:   return { "FX", "Risers" };
        case Family::Synth:     return { "Synths", "Synth", "Leads", "Pads" };
        default:                return {};
    }
}

void validateSourceForCategory(const UserPreset& p)
{
    const auto fam = familyOf(p.category);
    const auto allowed = expectedParentsFor(fam);
    if (fam == Family::Other || allowed.isEmpty() || p.source.path.isEmpty()) return;

    const auto norm = p.source.path.replaceCharacter('\\', '/');
    bool ok = false;
    for (auto& parent : allowed)
        if (norm.containsIgnoreCase("/" + parent + "/") || norm.startsWithIgnoreCase(parent + "/"))
            { ok = true; break; }

    if (! ok)
        didaUserPresetLog("WARNING preset '" + p.presetName + "' category=" + p.category
            + " uses unexpected source folder: " + p.source.path
            + " (expected one of: " + allowed.joinIntoString(", ") + ")");
}

struct EnvRange { float aMin, aMax, dMin, dMax, sMin, sMax, rMin, rMax; };

EnvRange envRangeFor(Family f)
{
    // Per-category envelope envelope ranges (seconds / 0..1 for sustain).
    switch (f) {
        case Family::PianoKeys: return { 0.0f, 0.008f, 0.30f, 0.90f, 0.20f, 0.55f, 0.25f, 0.90f };
        case Family::Lead:      return { 0.0f, 0.020f, 0.05f, 0.40f, 0.70f, 0.95f, 0.40f, 1.40f };
        case Family::Pad:       return { 0.12f, 0.80f, 0.20f, 1.50f, 0.80f, 1.00f, 1.80f, 6.00f };
        case Family::ChoirVox:  return { 0.10f, 0.60f, 0.20f, 1.20f, 0.75f, 1.00f, 1.40f, 4.50f };
        case Family::Brass:     return { 0.005f, 0.04f, 0.10f, 0.50f, 0.65f, 0.95f, 0.30f, 1.20f };
        case Family::Guitar:    return { 0.0f,   0.01f, 0.20f, 0.80f, 0.45f, 0.85f, 0.30f, 1.20f };
        case Family::Bell:      return { 0.0f,   0.005f, 0.20f, 0.80f, 0.05f, 0.40f, 0.60f, 2.20f };
        case Family::Pluck:     return { 0.0f,   0.005f, 0.10f, 0.40f, 0.00f, 0.25f, 0.20f, 0.80f };
        case Family::Bass808:   return { 0.0f,   0.005f, 0.10f, 0.40f, 0.85f, 1.00f, 0.10f, 0.60f };
        case Family::FxRiser:   return { 0.0f,   1.00f, 0.05f, 4.00f, 0.00f, 1.00f, 0.05f, 4.00f };
        // Vintage synth presets: warm, soft, never clicky and never too long.
        case Family::Synth:     return { 0.008f, 0.040f, 0.10f, 1.50f, 0.50f, 0.95f, 0.40f, 1.40f };
        default:                return { 0.0f,   2.00f, 0.01f, 4.00f, 0.00f, 1.00f, 0.01f, 6.00f };
    }
}

struct FxLimits { float chorusMax, delayMax, reverbMax, satMax; };

FxLimits fxLimitsFor(Family f)
{
    // Reasonable upper caps so e.g. piano presets can't ship at 80% reverb.
    // Aligned with the v2.1 category FX safety spec (May 2026 tuning pass).
    switch (f) {
        case Family::PianoKeys: return { 0.18f, 0.14f, 0.34f, 0.12f };
        case Family::Lead:      return { 0.40f, 0.45f, 0.35f, 0.40f };
        case Family::Pad:       return { 0.42f, 0.34f, 0.50f, 0.25f };
        case Family::ChoirVox:  return { 0.32f, 0.26f, 0.46f, 0.18f };
        case Family::Brass:     return { 0.20f, 0.12f, 0.28f, 0.28f };
        case Family::Guitar:    return { 0.34f, 0.35f, 0.42f, 0.24f };
        case Family::Bell:      return { 0.30f, 0.30f, 0.46f, 0.18f };
        case Family::Pluck:     return { 0.30f, 0.32f, 0.38f, 0.18f };
        case Family::Bass808:   return { 0.10f, 0.14f, 0.16f, 0.32f };
        case Family::FxRiser:   return { 0.50f, 0.55f, 0.48f, 0.50f };
        case Family::Synth:     return { 0.18f, 0.10f, 0.18f, 0.16f };
        default:                return { 0.45f, 0.40f, 0.45f, 0.38f };
    }

}

bool isChoirModePreset(const UserPreset& p)
{
    const auto c = p.category.toLowerCase();
    const auto n = p.presetName.toLowerCase();
    return p.choirMode || c.contains("choir") || c.contains("vox")
        || c.contains("vocal") || n.contains("choir");
}

bool isChoirWidePreset(const UserPreset& p)
{
    const auto n = p.presetName.toLowerCase();
    return n.contains("wide") || n.contains("heaven");
}

// Per-preset gain trim applied on top of preset.amp.gainDb when choir mode
// is active. Keeps choir presets sitting between -20..-10 dB instead of
// approaching 0 dBFS on chords.
float choirNaturalGainTrimDb(const UserPreset& p)
{
    const auto n = p.presetName.toLowerCase();
    if (n.contains("clean playable choir aah")) return  0.0f;  // +2 dB vs prior (-2)
    if (n.contains("dark controlled choir eeh")) return -14.0f; // -8 dB vs prior (-6)
    if (n.contains("wide heaven choir ooh"))    return -10.0f;  // -7 dB vs prior (-3)
    // Generic safety trim: choir samples are usually pre-normalised loud.
    return -3.0f;
}

float choirNaturalReverbMix(const UserPreset& p) noexcept
{
    const auto n = p.presetName.toLowerCase();
    if (n.contains("clean playable choir aah")) return 0.10f;
    if (n.contains("dark controlled choir eeh")) return 0.11f;
    if (n.contains("wide heaven choir ooh"))    return 0.15f;
    return isChoirWidePreset(p) ? 0.15f : 0.11f;
}

// True when this preset declares an enabled neuralTextureCached partial. File
// scope (anonymous) so the public isAiTexturePreset() and the gain trim share it.
bool presetHasNeuralTexturePartial(const UserPreset& p) noexcept
{
    for (const auto& pb : p.partials)
        if (pb.enabled
            && (pb.engineType.equalsIgnoreCase("neuralTextureCached")
             || pb.engineType.equalsIgnoreCase("neuralTexture")))
            return true;
    return false;
}

// Headroom cleanup (v0.2 quality pass): some AI Texture demo presets render hot
// because their fallback-synth body sits near 0 dBFS. Apply a per-preset amp
// trim so the FINAL output lands inside the category target window. Only AI
// Texture presets reach this (gated by loadTimeGainTrimDb).
float aiTextureGainTrimDb(const UserPreset& p) noexcept
{
    const auto n = p.presetName.toLowerCase();
    const auto c = p.category.toLowerCase();
    // Guitar Dust originally reported LOW_HEADROOM (~-7.8) when its body sat near
    // 0 dBFS. Now that the support is a controlled analog partial (-8 dB), a full
    // -8 dB global cut pushed the whole preset into TOO_QUIET, so a lighter -3 dB
    // headroom trim keeps it inside the -18..-8 dB window without going quiet.
    if (n.contains("guitar dust") || (c.contains("guitar") && presetHasNeuralTexturePartial(p)))
        return -3.0f;
    return 0.0f;
}

void configureAiTextureSupportAnalog(dida::engines::AnalogEngine& analog,
                                      const UserPreset::PartialBlock& pb) noexcept
{
    const juce::var ep = pb.engineParams;
    const auto osc = ep.getProperty("osc", "saw").toString().trim().toLowerCase();
    if (osc.contains("tri"))      analog.setShape(dida::engines::AnalogEngine::Shape::Tri);
    else if (osc.contains("square")) analog.setShape(dida::engines::AnalogEngine::Shape::Square);
    else if (osc.contains("pulse"))  analog.setShape(dida::engines::AnalogEngine::Shape::Pulse);
    else if (osc.contains("sine"))   analog.setShape(dida::engines::AnalogEngine::Shape::Sine);
    else if (osc.contains("noise"))  analog.setShape(dida::engines::AnalogEngine::Shape::Noise);
    else                             analog.setShape(dida::engines::AnalogEngine::Shape::Saw);

    analog.setUnison((int) ep.getProperty("unison", 1));
    analog.setDetune((float) (double) ep.getProperty("detuneCents", 0.0));
    analog.setStereoSpread((float) (double) ep.getProperty("spread", 0.25));
    analog.setSubLevel((float) (double) ep.getProperty("subLevel", 0.0));
    analog.setDrift((float) (double) ep.getProperty("driftCents", 0.0));

    const float attackMs  = juce::jlimit(5.0f, 20.0f,  (float) (double) ep.getProperty("attackMs", 12.0));
    const float decayMs   = juce::jlimit(1.0f, 300.0f, (float) (double) ep.getProperty("decayMs", 180.0));
    const float sustain   = juce::jlimit(0.65f, 1.0f,  (float) (double) ep.getProperty("sustain", 0.82));
    const float releaseMs = juce::jlimit(120.0f, 400.0f, (float) (double) ep.getProperty("releaseMs", 220.0));
    analog.setAmpEnvelopeMs(attackMs, decayMs, sustain, releaseMs);
}



juce::String fxSendReleaseSourceFor(const UserPreset& p, bool choirMode)
{
    // BUG 1/7: a preset-supplied value is ALWAYS presetFxSend, even in choir
    // mode. choirModeClamp only describes the case where the choir cap actually
    // had to LOWER an over-long release the preset did not explicitly request.
    if (p.fxSend.hasFxSendReleaseMs) return "presetFxSend";
    if (choirMode) return "choirModeClamp";
    if (p.fxSend.hasFxSendReleaseMultiplier) return "ampReleaseFallback";
    return "categoryDefault";
}

float resolveFxSendReleaseMs(const UserPreset& p, bool choirMode)
{
    if (choirMode)
    {
        // Choir clamp may only CAP the maximum (180ms). An explicit, lower
        // preset value must win — never get raised to a 40ms floor.
        if (p.fxSend.hasFxSendReleaseMs)
            return juce::jlimit(1.0f, 180.0f, p.fxSend.fxSendReleaseMs);

        const float requested = p.safety.hasChoirFxSendReleaseMaxMs
                ? p.safety.choirFxSendReleaseMaxMs
                : juce::jmin(180.0f, p.amp.releaseMs * (p.fxSend.hasFxSendReleaseMultiplier
                    ? p.fxSend.fxSendReleaseMultiplier : 0.35f));
        // BUG 1: the choir clamp is an UPPER cap only. The 40ms floor is gone —
        // a short requested release stays short (never raised to 40ms).
        return juce::jlimit(1.0f, 180.0f, requested);
    }

    if (p.fxSend.hasFxSendReleaseMs)
        return juce::jlimit(1.0f, 500.0f, p.fxSend.fxSendReleaseMs);

    if (p.fxSend.hasFxSendReleaseMultiplier)
        return juce::jlimit(1.0f, 500.0f, p.amp.releaseMs * p.fxSend.fxSendReleaseMultiplier);

    return 80.0f;
}

void applyLayerBusCharacter(juce::AudioProcessor& proc, const UserPreset& p, Family fam)
{
    auto* dp = dynamic_cast<DiditagainProcessor*>(&proc);
    if (dp == nullptr) return;
    auto& bus = dp->getSynthEngine().getLayerBus();

    // Macro-driven layer-bus voicing.
    const float warmth   = juce::jlimit(0.0f, 1.0f, p.macros.warmth);
    const float width    = juce::jlimit(0.0f, 1.0f, p.macros.width);
    const float movement = juce::jlimit(0.0f, 1.0f, p.macros.movement);
    const bool naturalChoir = isChoirModePreset(p);

    bus.setEnabled(true);
    if (naturalChoir)
    {
        bus.setSaturationDrive(0.0f);
        bus.setSaturationMix(0.0f);
        bus.setWidth(juce::jlimit(0.0f, 1.4f, 0.85f * (0.6f + width * 0.5f)));
        bus.setDriftRate(0.0f);
        bus.setDriftDepth(0.0f);
        return;
    }

    // Synth family gets much gentler glue so the shared tanh doesn't pile on
    // top of per-voice saturation. Other families keep their original curve.
    const bool synthFam = (fam == Family::Synth);
    const float satDriveMax = synthFam ? 0.16f : 0.38f;
    const float satMixMax   = synthFam ? 0.14f : 0.45f;
    const float satDriveBase = synthFam ? 0.04f : 0.08f;
    const float satMixBase   = synthFam ? 0.06f : 0.10f;
    bus.setSaturationDrive(juce::jmin(satDriveMax, satDriveBase + warmth * (satDriveMax - satDriveBase)));
    bus.setSaturationMix  (juce::jmin(satMixMax,   satMixBase   + warmth * (satMixMax   - satMixBase)));

    // Per-family width baseline (then macro nudges it).
    float baseWidth = 0.85f;
    switch (fam) {
        case Family::PianoKeys: baseWidth = 0.75f; break;
        case Family::Lead:      baseWidth = 0.88f; break;
        case Family::Pad:       baseWidth = 1.00f; break;
        case Family::ChoirVox:  baseWidth = 1.05f; break;
        case Family::Brass:     baseWidth = 0.80f; break;
        case Family::Guitar:    baseWidth = 0.78f; break;
        case Family::Bass808:   baseWidth = 0.35f; break; // narrow low end
        case Family::Synth:     baseWidth = 0.80f; break;
        default: break;
    }
    bus.setWidth(juce::jlimit(0.0f, 1.4f, baseWidth * (0.6f + width * 0.8f)));

    // Shared modulation depth (subtle unless macro pushes it).
    bus.setDriftRate (0.12f + movement * 0.30f);
    bus.setDriftDepth(0.006f + movement * 0.025f);
}

void clampEnvelopeForCategory(juce::AudioProcessor& proc, const UserPreset& p, Family fam)
{
    const auto r = envRangeFor(fam);
    const float aSec = juce::jlimit(r.aMin, r.aMax, p.amp.attackMs  / 1000.0f);
    const float dSec = juce::jlimit(r.dMin, r.dMax, p.amp.decayMs   / 1000.0f);
    const float sus  = juce::jlimit(r.sMin, r.sMax, p.amp.sustain);
    const float rSec = juce::jlimit(r.rMin, r.rMax, p.amp.releaseMs / 1000.0f);

    setParamById(proc, "env1Attack",  aSec);
    setParamById(proc, "env1Decay",   dSec);
    setParamById(proc, "env1Sustain", sus);
    setParamById(proc, "env1Release", rSec);
}

} // anonymous

// Public: AI Texture (cached neural) preset detection. See header.
bool isAiTexturePreset(const UserPreset& p) noexcept
{
    if (presetHasNeuralTexturePartial(p)) return true;
    const auto pr = p.ai.provider.toLowerCase();
    return p.ai.present && (pr == "cachedtexture" || pr == "demopack");
}



// Public: reproduce the engine's load-time gain trim so the quality report can
// compute the true applied amp gain without a stale live read. Choir-mode
// presets get the choir natural trim; AI Texture presets get a headroom trim;
// everything else returns 0.
float loadTimeGainTrimDb(const UserPreset& p)
{
    // AI Texture presets take priority over the choir path: a cached-texture
    // choir preset (e.g. "AI Choir Ghost Test") must use the AI texture trim so
    // its installed support body is not also cut by the natural-choir trim.
    if (isAiTexturePreset(p)) return aiTextureGainTrimDb(p);
    if (isChoirModePreset(p)) return choirNaturalGainTrimDb(p);
    return 0.0f;
}

void applyToProcessor(const UserPreset& p, juce::AudioProcessor& proc)
{
    const Family fam = familyOf(p.category);
    const bool choirMode = isChoirModePreset(p);

    // -- Source / category validation (non-fatal warning only)
    validateSourceForCategory(p);

    // -- Gain staging (Report 78/81): amp.gainDb is the PRIMARY preset loudness
    //    and is routed verbatim to the dedicated ampGain stage, plus an explicit
    //    additive load-time trim (choir-mode only) and the engine's [-60,+24] dB
    //    safety clamp. NOTHING from the previous preset is carried in: the trim
    //    is recomputed from THIS preset every load and the master trim is reset
    //    to a neutral 0 dB on EVERY load.
    const float loadTimeTrimDb     = loadTimeGainTrimDb(p);   // 0 unless choir-mode
    const float requestedAmpGainDb = p.amp.gainDb;
    const float finalAmpGainDb     = juce::jlimit(-60.0f, 24.0f, requestedAmpGainDb + loadTimeTrimDb);
    setParamById(proc, "ampGain",    finalAmpGainDb);
    setParamById(proc, "masterGain", 0.0f);
    if (auto* dp = dynamic_cast<DiditagainProcessor*>(&proc))
        dp->getSynthEngine().getFx().noteRequestedMasterGainDb(0.0f);

    // BUG 2 (Report 81): per-load gain trace. Logs the EXACT amp gain applied
    // this load alongside the previous load's final amp gain so any one-preset-
    // late inheritance is immediately visible.
    {
        static float previousFinalAmpGainDb = 0.0f;
        const bool changedThisLoad = std::abs(finalAmpGainDb - previousFinalAmpGainDb) > 0.001f;
        juce::Logger::writeToLog(juce::String("[DIDITAGAIN gain] PRESET_GAIN_APPLIED preset=")
            + p.presetName
            + " requestedAmpGainDb=" + juce::String(requestedAmpGainDb, 2)
            + " loadTimeGainTrimDb=" + juce::String(loadTimeTrimDb, 2)
            + " finalAmpGainDb=" + juce::String(finalAmpGainDb, 2)
            + " previousFinalAmpGainDb=" + juce::String(previousFinalAmpGainDb, 2)
            + " changedThisLoad=" + (changedThisLoad ? "true" : "false"));
        previousFinalAmpGainDb = finalAmpGainDb;
    }
    if (std::abs(loadTimeTrimDb) > 0.001f)
        juce::Logger::writeToLog(juce::String("[DIDITAGAIN choir-safety] preset=")
            + p.presetName + " effect=ampGain"
            + " oldGainDb=" + juce::String(p.amp.gainDb, 2)
            + " newGainDb=" + juce::String(finalAmpGainDb, 2)
            + " reason=choirNaturalGainTrim");

    // -- Amp env: clamped to per-category musical ranges
    clampEnvelopeForCategory(proc, p, fam);

    // -- Filter
    setChoiceById(proc, "filter1Type", filterTypeIndex(p.filter.type));
    setParamById(proc, "filter1Cutoff",    p.filter.enabled ? p.filter.cutoffHz : 20000.0f);
    setParamById(proc, "filter1Resonance", p.filter.resonance);
    setParamById(proc, "filter1Drive",     p.filter.drive);
    setParamById(proc, "filter1KeyTrack",  p.filter.keytrack);

    // -- Layer 2 (octave/detune routed onto Osc B controls).
    //
    // Reinforcement-layer safety: for PCM/sample-based categories the
    // oscillator layer is meant to thicken the sample, NOT sit on top as a
    // pure sine beep. We hard-clamp the layer gain to -15 dB unless the
    // preset is explicitly experimental or a true synth-lead/synth-only
    // family (Synth/Lead/Bass808/FxRiser) where loud oscillator layers are
    // legitimate.
    const bool isPcmFamily =
           fam == Family::PianoKeys || fam == Family::Pad
        || fam == Family::ChoirVox  || fam == Family::Brass
        || fam == Family::Guitar    || fam == Family::Bell
        || fam == Family::Pluck     || fam == Family::Other;

    // ------- BlendMode-driven layer-2 contract -------
    // Resolve the effective blend mode: explicit JSON wins; otherwise pick a
    // sane default per family. Each mode carries its own max-gain cap and
    // sine-on-PCM swap policy. Experimental presets opt out of clamping.
    auto resolveBlendMode = [&]() -> juce::String
    {
        const auto explicitMode = p.layer2.blendMode.trim().toLowerCase();
        if (explicitMode.isNotEmpty()) return explicitMode;
        switch (fam)
        {
            case Family::Brass:     return "addwarmth";
            case Family::Guitar:    return "hiddentexture";
            case Family::PianoKeys: return "addair";
            case Family::ChoirVox:  return "addair";
            case Family::Pad:       return "addair";
            case Family::Bell:      return "addair";
            case Family::Pluck:     return "hiddentexture";
            case Family::Bass808:   return "subsupport";
            case Family::Lead:      return "leadlayer";
            case Family::Synth:     return "leadlayer";
            default:                return "reinforcebody";
        }
    };

    struct BlendPolicy { float maxGainDb; float defaultGainDb; bool swapSineToTriangle; };
    auto policyFor = [](const juce::String& mode) -> BlendPolicy
    {
        if (mode == "leadlayer")     return { 0.0f,    -3.0f,  false };
        if (mode == "subsupport")    return { -12.0f, -15.0f,  false };
        if (mode == "reinforcebody") return { -15.0f, -18.0f,  true  };
        if (mode == "addwarmth")     return { -18.0f, -22.0f,  true  };
        if (mode == "addair")        return { -22.0f, -26.0f,  false }; // sine is fine, just quiet
        if (mode == "hiddentexture") return { -28.0f, -32.0f,  false };
        return { -15.0f, -22.0f, true }; // unknown -> safest PCM cap
    };

    float layer2GainDb = p.layer2.gainDb;
    if (p.layer2.enabled && ! p.experimental)
    {
        const auto mode    = resolveBlendMode();
        const auto policy  = policyFor(mode);

        // Explicit per-preset maxGainDb beats the mode default, but never
        // overrides the family ceiling for PCM presets.
        float effectiveMax = policy.maxGainDb;
        if (p.layer2.maxGainDb < 0.0f)
            effectiveMax = juce::jmin(effectiveMax, p.layer2.maxGainDb);

        // subSupport is only musical for Bass808 — fold to addAir for everyone else.
        const bool subSupportInvalid = (mode == "subsupport") && (fam != Family::Bass808);

        // Only PCM/sample families enforce the clamp; true synth families
        // can run loud oscillator layers as their main sound.
        const bool enforce = isPcmFamily || subSupportInvalid;
        if (enforce && layer2GainDb > effectiveMax)
        {
            juce::Logger::writeToLog(juce::String("[DIDITAGAIN layer-safety] preset=")
                + p.presetName + " layer=layer2"
                + " blendMode=" + mode
                + " reason=" + (subSupportInvalid ? juce::String("subSupport invalid for category")
                                                  : juce::String("over blendMode cap"))
                + " oldGain=" + juce::String(layer2GainDb, 1)
                + " newGain=" + juce::String(policy.defaultGainDb, 1)
                + " category=" + p.category);
            layer2GainDb = policy.defaultGainDb;
        }

        // Auto-swap raw Sine → Triangle when the blend mode says so. Lead/
        // air/subSupport presets are allowed to keep a sine since they're
        // either supposed to be loud (lead) or already bandwidth-limited.
        if (policy.swapSineToTriangle && isPcmFamily)
        {
            for (auto* param : proc.getParameters())
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(param))
                    if (c->paramID == "oscBWaveform" && c->getIndex() == 0 /*Sine*/)
                    {
                        setParamRaw(c, c->convertTo0to1(1.0f)); // Triangle
                        juce::Logger::writeToLog(juce::String("[DIDITAGAIN layer-safety] preset=")
                            + p.presetName + " layer=layer2 blendMode=" + mode
                            + " swappedSineToTriangle=1 category=" + p.category);
                        break;
                    }
        }
    }

    // ---- Choir natural mode: force sample-only playback. Long vocal WAVs
    //      already contain the tone; oscillator/air support layers turn Eeh/Ooh
    //      into synth pads, so never allow layer2 in natural choir mode.
    bool   choirSyntheticLayerDisabled = false;
    float  choirLayer2GainDbOut = layer2GainDb;
    bool   effectiveLayer2Enabled = p.layer2.enabled;
    if (choirMode)
    {
        effectiveLayer2Enabled = false;
        choirSyntheticLayerDisabled = true;
        choirLayer2GainDbOut = -120.0f;
        if (p.layer2.enabled || layer2GainDb > -120.0f)
            juce::Logger::writeToLog(juce::String("[DIDITAGAIN choir-safety] preset=")
                + p.presetName + " layer=layer2 reason=natural choir mode forced sample-only"
                + " oldGainDb=" + juce::String(layer2GainDb, 2));
    }

    setParamById(proc, "oscBLevel",  effectiveLayer2Enabled
        ? juce::Decibels::decibelsToGain(choirLayer2GainDbOut) : 0.0f);
    setParamById(proc, "oscBOctave", choirMode ? 0.0f : static_cast<float>(p.layer2.octave));
    setParamById(proc, "oscBSemi",   choirMode ? 0.0f : static_cast<float>(p.layer2.semitone));
    setParamById(proc, "oscBDetune", choirMode ? 0.0f : p.layer2.detuneCents);

    // ---- eqRole wiring: per-layer carver cutoffs ----
    //
    // Resolve an effective eqRole for layer 2 from the explicit field, falling
    // back to the blend mode that the policy block above picked. This lets a
    // preset say `blendMode: "addAir"` and automatically inherit `eqRole:
    // "air"` (oscBHp opened up to 2.5 kHz) without authoring both fields.
    {
        juce::String role2 = p.layer2.eqRole.trim().toLowerCase();
        if (role2.isEmpty())
        {
            const auto mode = (p.layer2.blendMode.trim().toLowerCase().isNotEmpty()
                                  ? p.layer2.blendMode.trim().toLowerCase()
                                  : juce::String());
            if      (mode == "addair")        role2 = "air";
            else if (mode == "addwarmth")     role2 = "warmth";
            else if (mode == "reinforcebody") role2 = "body";
            else if (mode == "hiddentexture") role2 = "texture";
            else if (mode == "subsupport")    role2 = "sub";
        }

        if (auto* dp = dynamic_cast<DiditagainProcessor*>(&proc))
        {
            const juce::String r2 = role2;
            // PCM/sample families default followMainEnvelope=true so
            // reinforcement layers always ease in under the main attack.
            const bool followFromPreset = p.layer2.followMainEnvelope;
            const bool followEffective  = isPcmFamily ? true : followFromPreset;
            const float mainAttackMs    = p.amp.attackMs;
            dp->getSynthEngine().forEachSynthVoice([&r2, followEffective, mainAttackMs](SynthVoice& v)
            {
                v.setLayer2EqRole(r2);
                // Layers 3/4 inherit complementary roles so noise/sub carvers
                // also park in their own bands. We don't have dedicated
                // LayerBlocks for them yet — use sensible siblings.
                v.setLayer3EqRole(r2 == "air" || r2 == "texture" ? r2 : juce::String("air"));
                v.setLayer4EqRole(r2 == "sub" ? juce::String("sub")
                                  : r2 == "warmth" ? juce::String("warmth")
                                                   : juce::String("sub"));
                v.setLayer2FollowMain(followEffective, mainAttackMs);
                v.setLayer3FollowMain(followEffective, mainAttackMs);
                v.setLayer4FollowMain(followEffective, mainAttackMs);
            });
        }
    }



    // Sample is the only sound source — make sure Osc A pass-through is on,
    // sub/noise silenced. (Same convention as PresetManager sample-drop path.)
    setParamById(proc, "oscALevel",    1.0f);
    setParamById(proc, "subOscLevel",  0.0f);
    setParamById(proc, "noiseLevel",   0.0f);
    if (choirMode)
    {
        setParamById(proc, "oscADetune", 0.0f);
        setParamById(proc, "oscAOctave", 0.0f);
        setParamById(proc, "oscASemi",   0.0f);
        setParamById(proc, "unisonVoices", 1.0f);
        setParamById(proc, "unisonDetune", 0.0f);
        setParamById(proc, "unisonSpread", 0.0f);
        setParamById(proc, "vintageAmount", 0.0f);
    }
    for (auto* param : proc.getParameters())
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(param))
            if (b->paramID == "subOscEnabled") setParamRaw(b, 0.0f);

    // -- FX: clamp each mix to a category-sane upper limit so e.g. piano
    //        presets cannot ship as a wet-bath. Macro "space" can nudge
    //        reverb mix up to the cap.
    const auto fxL = fxLimitsFor(fam);
    const float space   = juce::jlimit(0.0f, 1.0f, p.macros.space);
    // Reverb is silenced when the preset disables it OR explicitly bypasses it.
    // bypass==true must force the reverb return to absolute silence (-120 dB)
    // regardless of any mix value the preset ships with.
    const bool reverbSilenced = (! p.reverb.enabled) || p.reverb.bypass;
    const float reverbBase = reverbSilenced ? 0.0f : p.reverb.mix;
    const bool choirIsWide = choirMode && isChoirWidePreset(p);
    // Natural choir mode uses fixed room levels per preset so vocal samples
    // stay realistic; macro space is ignored here to avoid pad-style wash.
    const float reverbCap = choirMode ? choirNaturalReverbMix(p) : fxL.reverbMax;
    const float delayCap  = choirMode ? 0.00f : fxL.delayMax;
    const float reverbSizeCap = choirMode ? (choirIsWide ? 0.62f : 0.55f) : 1.0f;
    const float delayFeedbackCap = choirMode ? 0.00f : 0.95f;
    // Choir natural reverb is a CAP/default only — it may never RAISE an
    // explicit (lower) preset reverb mix, and a silenced/bypassed preset reverb
    // must stay at 0 (BUG 2). For non-choir presets the macro "space" nudge is
    // still allowed up to the category cap.
    const float reverbMix  = reverbSilenced ? 0.0f
                           : (choirMode ? juce::jmin(reverbBase, reverbCap)
                                        : juce::jlimit(0.0f, reverbCap,
                                                       reverbBase * (0.85f + space * 0.30f)));
    // Delay off => mix AND feedback forced to zero so the delay return reads
    // -120 dB and no tail can sustain itself through feedback.
    const float delayMix = p.delay.enabled ? juce::jlimit(0.0f, delayCap, p.delay.mix) : 0.0f;
    const float delayFeedback = p.delay.enabled ? juce::jlimit(0.0f, delayFeedbackCap, p.delay.feedback) : 0.0f;
    const float reverbSize = juce::jlimit(0.0f, reverbSizeCap, p.reverb.size);

    // Chorus: off for natural choir except a barely audible Ooh/Heaven width.
    const float chorusWanted = p.chorus.enabled ? p.chorus.mix : 0.0f;
    float chorusCap = fxL.chorusMax;
    if (choirMode)
        chorusCap = choirIsWide ? 0.015f : 0.0f;
    const float chorusMixOut = juce::jlimit(0.0f, chorusCap, chorusWanted);
    setParamById(proc, "fxChorusMix", chorusMixOut);
    setParamById(proc, "fxDelayMix", delayMix);
    setParamById(proc, "fxDelayTime",        p.delay.timeMs / 1000.0f);
    setParamById(proc, "fxDelayFeedback",    delayFeedback);
    setParamById(proc, "fxReverbMix",        reverbMix);
    setParamById(proc, "fxReverbSize",       reverbSize);
    // Saturation: off entirely for choir natural mode.
    const float satDrive = choirMode ? 0.0f
                                     : juce::jlimit(0.0f, fxL.satMax,
                                                    p.saturation.enabled ? p.saturation.drive : 0.0f);
    setParamById(proc, "fxDistortionAmount", satDrive);
    // Saturation mix: clamp tighter for Synth family so vintage presets stay
    // warm instead of distorted. Other families also get a reasonable cap.
    const float satMixCap = (fam == Family::Synth) ? 0.18f : 0.50f;
    const float satMix = choirMode ? 0.0f
                                   : (p.saturation.enabled ? juce::jlimit(0.0f, satMixCap, p.saturation.mix) : 0.0f);
    setParamById(proc, "fxSaturationMix", satMix);

    if (choirMode)
    {
        if (std::abs(p.delay.feedback - delayFeedback) > 0.001f)
            juce::Logger::writeToLog(juce::String("[DIDITAGAIN choir-safety] effect=delay")
                + " oldFeedback=" + juce::String(p.delay.feedback, 3)
                + " newFeedback=" + juce::String(delayFeedback, 3)
                + " reason=choirMode");
        if (std::abs((p.delay.enabled ? p.delay.mix : 0.0f) - delayMix) > 0.001f)
            juce::Logger::writeToLog(juce::String("[DIDITAGAIN choir-safety] effect=delay")
                + " oldMix=" + juce::String(p.delay.enabled ? p.delay.mix : 0.0f, 3)
                + " newMix=" + juce::String(delayMix, 3)
                + " reason=choirMode");
    }

    // -- LFOs (rate + shape; routing matrix is owned by the engine elsewhere)
    setParamById(proc, "lfo1Rate", p.lfo1.rateHz);
    setChoiceById(proc, "lfo1Shape", lfoShapeIndex(p.lfo1.shape));
    setParamById(proc, "lfo2Rate", p.lfo2.rateHz);
    setChoiceById(proc, "lfo2Shape", lfoShapeIndex(p.lfo2.shape));

    // -- Subtle filter movement (only if explicitly enabled, or experimental)
    if (p.filterMovement.enabled || p.experimental)
    {
        const float depth = p.experimental ? p.filterMovement.depth
                                           : juce::jlimit(0.0f, 0.35f, p.filterMovement.depth);
        setParamById(proc, "lfo1Rate", p.filterMovement.rateHz);
        juce::ignoreUnused(depth); // routed via ModMatrix; rate is the audible bit
    }

    // -- Velocity sensitivity (persists on processor params used by Voice)
    setParamById(proc, "velocityToGain",   juce::jlimit(0.0f, 1.0f, p.velocity.toGain));
    setParamById(proc, "velocityToCutoff", juce::jlimit(0.0f, 1.0f, p.velocity.toCutoff));
    setParamById(proc, "velocityToAttack", juce::jlimit(0.0f, 1.0f, p.velocity.toAttack));
    setParamById(proc, "velocityToLayer",  juce::jlimit(0.0f, 1.0f, p.velocity.toLayerBlend));

    // -- Premium voicing: shared layer-bus + per-category reverb character.
    applyLayerBusCharacter(proc, p, fam);
    dida::preset::applyReverbCharacterForCategory(proc, p.category);
    if (auto* dp = dynamic_cast<DiditagainProcessor*>(&proc))
    {
        auto& engine = dp->getSynthEngine();
        if (choirMode)
        {
            const float fxSendReleaseMs = resolveFxSendReleaseMs(p, true);
            engine.setFxSendReleaseMsForAll(fxSendReleaseMs);
            auto& pfx = engine.getFx();
            pfx.setChoirDensityMode(true);
            // BUG 3: latch hard-bypass BEFORE pushing mixes and drain tails so
            // the per-block macro re-apply can't revive a silenced FX, and no
            // stale reverb tail bleeds into the new preset.
            pfx.clearTimeFxTails();
            pfx.setReverbHardBypass(reverbSilenced || reverbMix <= 0.0001f);
            pfx.setDelayHardBypass(! p.delay.enabled || delayMix <= 0.0001f);
            pfx.setDelayMix(delayMix);
            pfx.setDelayFeedback(delayFeedback);
            pfx.setReverbMix(reverbMix);
            pfx.setReverbSize(reverbSize);
            pfx.setReverbInputHighPassHz(300.0f);
            pfx.setReverbInputLowPassHz(5500.0f);
            pfx.setReverbDucking(0.32f, 6.0f, 220.0f);
            pfx.setDelayDucking(0.50f, 5.0f, 140.0f);
            pfx.setNoteDensityFxReductionEnabled(true);
            pfx.setNoteDensityMaxReduction(0.35f);
            pfx.setDelayDensityWeight(1.0f);
            pfx.setReverbDensityWeight(0.75f);
            juce::Logger::writeToLog(juce::String("[DIDITAGAIN choir-fx-send] preset=") + p.presetName
                + " fxSendReleaseMs=" + juce::String(fxSendReleaseMs, 1)
                + " fxSendReleaseSource=" + fxSendReleaseSourceFor(p, true)
                + " noteOffStopsFxSend=" + (p.fxSend.noteOffStopsFxSend ? "true" : "false"));
        }
        else
        {
            auto& pfx = engine.getFx();
            pfx.setChoirDensityMode(false);
            pfx.setNoteDensityMaxReduction(0.32f);
            pfx.setDelayDensityWeight(1.0f);
            pfx.setReverbDensityWeight(1.0f);

            // Task 1: non-choir presets must push their own FX-send release time
            // so the engine no longer keeps the 80 ms default.
            const float fxSendReleaseMs = resolveFxSendReleaseMs(p, false);
            engine.setFxSendReleaseMsForAll(fxSendReleaseMs);

            // Non-choir latch wiring: apply the same hard-bypass + tail-drain
            // discipline as the choir branch. Latch BEFORE pushing mixes so the
            // per-block macro re-apply in processBlock cannot revive a silenced
            // reverb/delay, and clear stale tails from the previous preset so a
            // disabled/bypassed effect reads -120 dB at its return (BUG 3/6).
            pfx.clearTimeFxTails();
            pfx.setReverbHardBypass(reverbSilenced || reverbMix <= 0.0001f);
            pfx.setDelayHardBypass(! p.delay.enabled || delayMix <= 0.0001f);

            // Tasks 2/3: mirror the clamped/zeroed mix + feedback onto the live
            // FX chain. When reverb is disabled/bypassed or delay is off these
            // are zero, forcing the returns to -120 dB.
            pfx.setDelayMix(delayMix);
            pfx.setDelayFeedback(delayFeedback);
            pfx.setReverbMix(reverbMix);
            pfx.setReverbSize(reverbSize);

            // Task 4: preset reverb overrides win over the category defaults
            // that applyReverbCharacterForCategory() just applied above. Only
            // fields the preset explicitly set (>=0 / hasDucking) are pushed.
            if (p.reverb.preDelayMs >= 0.0f)      pfx.setReverbPreDelayMs(p.reverb.preDelayMs);
            if (p.reverb.inputHighpassHz >= 0.0f) pfx.setReverbInputHighPassHz(p.reverb.inputHighpassHz);
            if (p.reverb.inputLowpassHz >= 0.0f)  pfx.setReverbInputLowPassHz(p.reverb.inputLowpassHz);
            if (p.reverb.hasDucking)
            {
                const float duckAmt = (! p.reverb.duckingEnabled) ? 0.0f
                                      : (p.reverb.duckingAmount >= 0.0f ? p.reverb.duckingAmount : 0.0f);
                if (p.reverb.duckingAmount >= 0.0f || ! p.reverb.duckingEnabled)
                    pfx.setReverbDucking(duckAmt);
            }

            juce::Logger::writeToLog(juce::String("[DIDITAGAIN fx-send] preset=") + p.presetName
                + " fxSendReleaseMs=" + juce::String(fxSendReleaseMs, 1)
                + " fxSendReleaseSource=" + fxSendReleaseSourceFor(p, false)
                + " reverbMix=" + juce::String(reverbMix, 3)
                + " reverbSilenced=" + (reverbSilenced ? "true" : "false")
                + " delayMix=" + juce::String(delayMix, 3)
                + " delayFeedback=" + juce::String(delayFeedback, 3));
        }
    }

    // -- Mod-matrix translation: for the routings the engine natively
    //    understands, fold the entry's amount into the matching APVTS param
    //    so existing audio paths apply the modulation. Unknown pairs are
    //    silently ignored here (still preserved on save).
    for (auto& e : p.modMatrix)
    {
        const auto s = e.source.toLowerCase();
        const auto d = e.dest.toLowerCase();
        const float a = juce::jlimit(-1.0f, 1.0f, e.amount);

        // velocity -> amp / cutoff / attack / layer-blend
        if (s == "velocity")
        {
            if      (d == "amp.gain"     || d == "amp.gaindb") setParamById(proc, "velocityToGain",   std::abs(a));
            else if (d == "filter1cutoff"|| d == "filter.cutoffhz") setParamById(proc, "velocityToCutoff", std::abs(a));
            else if (d == "amp.attack"   || d == "env1.attack")     setParamById(proc, "velocityToAttack", std::abs(a));
            else if (d == "layer.blend"  || d == "layer2.gaindb")   setParamById(proc, "velocityToLayer",  std::abs(a));
        }
        // env1 -> filter cutoff (signed envelope amount on Voice)
        else if ((s == "env1" || s == "envelope1") && (d == "filter1cutoff" || d == "filter.cutoffhz"))
        {
            setParamById(proc, "filterEnvAmount", juce::jlimit(-1.0f, 1.0f, a));
        }
        // lfo1 -> filter cutoff: keep rate, scale depth via filterMovement-style depth knob if present
        else if ((s == "lfo1") && (d == "filter1cutoff" || d == "filter.cutoffhz"))
        {
            // rate already set above; nothing else to do here without a depth param.
        }
        // modwheel -> vibrato (mapped through pitch wheel range via existing CC11/CC1 path in Voice::controllerMoved)
        // aftertouch -> cutoff handled by Voice::controllerMoved; nothing to write here.
    }

    // NOTE: we deliberately do NOT push "polyphony" into APVTS here. Polyphony
    // is a global engine setting; writing it on every preset load triggers the
    // deferred voice-pool mutation path in PluginProcessor, which can refuse
    // to apply while the user is holding notes — and that gate previously
    // suppressed the sample-folder swap, leaving subsequent presets silent.
    juce::ignoreUnused(p.advanced.polyphony);

    didaUserPresetLog("applied name=" + p.presetName
        + " category=" + p.category
        + " src=" + p.source.path
        + " amp.gainDb=" + juce::String(p.amp.gainDb, 2)
        + " filter=" + p.filter.type
        + " cutoff=" + juce::String(p.filter.cutoffHz, 0)
        + " reverbMix(clamped)=" + juce::String(reverbMix, 2)
        + " delayMix=" + juce::String(juce::jmin(p.delay.mix, fxL.delayMax), 2)
        + " chorusMix=" + juce::String(juce::jmin(p.chorus.mix, fxL.chorusMax), 2)
        + " macros.warmth=" + juce::String(p.macros.warmth, 2)
        + " macros.space="  + juce::String(p.macros.space, 2));

    // Task 5/9/10: PRESET_APPLIED diagnostic. Surfaces the gain-staging and
    // layer/partial state that determine whether a preset can make sound, so
    // "silent" presets (e.g. Clean Tuned Piano) reveal main-layer/amp-gain or
    // zone-mapping issues directly in the host console.
    {
        int enabledPartials = 0;
        for (auto& pb : p.partials) if (pb.enabled) ++enabledPartials;
        const float appliedAmpGainDb = juce::jlimit(-60.0f, 24.0f, p.amp.gainDb + loadTimeGainTrimDb(p));
        didaUserPresetLog("PRESET_APPLIED name=" + p.presetName
            + " category=" + p.category
            + " mainLayerEnabled=" + (p.main.enabled ? "true" : "false")
            + " mainGainDb=" + juce::String(p.main.gainDb, 2)
            + " layer2Enabled=" + (p.layer2.enabled ? "true" : "false")
            + " layer2GainDb=" + juce::String(p.layer2.gainDb, 2)
            + " enabledPartials=" + juce::String(enabledPartials)
            + " ampGainDb=" + juce::String(p.amp.gainDb, 2)
            + " appliedAmpGainDb=" + juce::String(appliedAmpGainDb, 2)
            + " appliedMasterGainDb=0.00"
            + " engineType=" + (p.engineType.isNotEmpty() ? p.engineType : juce::String("pcm"))
            + " sourcePath=" + p.source.path
            + " reverbEnabled=" + (p.reverb.enabled ? "true" : "false")
            + " reverbBypass=" + (p.reverb.bypass ? "true" : "false")
            + " delayEnabled=" + (p.delay.enabled ? "true" : "false"));
    }

    // Engine summary log — makes the active engine list visible in the host
    // console so users can confirm which engine(s) a preset is exercising.
    if (! p.partials.isEmpty() || p.engineType.isNotEmpty())
    {
        juce::StringArray engines;
        if (! p.partials.isEmpty())
        {
            for (auto& pb : p.partials)
                if (pb.enabled) engines.add(pb.engineType.isNotEmpty() ? pb.engineType : juce::String("pcm"));
        }
        else
        {
            engines.add(p.engineType);
        }
        didaUserPresetLog("engines name=" + p.presetName
            + " active=[" + engines.joinIntoString(",") + "]"
            + " partials=" + juce::String(p.partials.size()));
    }

    // ---- Instantiate partial engines on every voice ----------------------
    if (auto* dp = dynamic_cast<DiditagainProcessor*>(&proc))
    {
        // AI Texture v0.1: decode each cached neural texture WAV ONCE here on
        // the message thread, then SHARE the immutable buffer with every voice's
        // engine instance. No file IO ever happens on the audio thread.
        struct TextureCacheEntry
        {
            std::shared_ptr<const juce::AudioBuffer<float>> buffer;
            double sampleRate = 44100.0;
            bool   missing    = true;
        };
        std::map<juce::String, TextureCacheEntry> textureCache;

        auto resolveTexture = [&](const juce::String& rawPath) -> TextureCacheEntry&
        {
            auto it = textureCache.find(rawPath);
            if (it != textureCache.end()) return it->second;

            TextureCacheEntry entry;
            if (rawPath.isNotEmpty())
            {
                const juce::File f = resolveSourcePath(rawPath);
                dida::engines::NeuralTextureEngine loader;
                if (loader.loadTextureFile(f))
                {
                    // Pull the decoded buffer out via a tiny second engine load.
                    // (loadTextureFile stores it internally; re-expose by loading
                    //  into a shared buffer here.)
                    juce::AudioFormatManager fm; fm.registerBasicFormats();
                    if (auto* reader = fm.createReaderFor(f))
                    {
                        std::unique_ptr<juce::AudioFormatReader> r(reader);
                        const int len = (int) juce::jmin<juce::int64>(
                            r->lengthInSamples, 60 * (juce::int64) r->sampleRate);
                        auto buf = std::make_shared<juce::AudioBuffer<float>>(2, juce::jmax(1, len));
                        buf->clear();
                        r->read(buf.get(), 0, len, 0, true, r->numChannels > 1);
                        if (r->numChannels == 1) buf->copyFrom(1, 0, *buf, 0, 0, len);
                        entry.buffer     = buf;
                        entry.sampleRate = r->sampleRate;
                        entry.missing    = false;
                    }
                }
            }
            // AI Texture v0.1 debug: log once per unique texture path whether the
            // cached WAV resolved. This happens at preset-apply time only (message
            // thread), never per audio block, so it cannot spam the log.
            didaUserPresetLog(juce::String("aiTexture resolve path=\"") + rawPath + "\""
                + " resolved=" + (entry.missing ? "false" : "true")
                + (entry.missing ? juce::String("")
                                 : " sr=" + juce::String(entry.sampleRate, 0)
                                   + " samples=" + juce::String(entry.buffer != nullptr
                                        ? entry.buffer->getNumSamples() : 0)));
            auto res = textureCache.emplace(rawPath, std::move(entry));
            return res.first->second;
        };

        auto makeEngine = [](const juce::String& t)
            -> std::unique_ptr<dida::engines::IEngineSource>
        {
            switch (dida::engines::engineTypeFromString(t))
            {
                case dida::engines::EngineType::Analog:    return std::make_unique<dida::engines::AnalogEngine>();
                case dida::engines::EngineType::Supersaw:  return std::make_unique<dida::engines::SupersawEngine>();
                case dida::engines::EngineType::Fm:        return std::make_unique<dida::engines::FmEngine>();
                case dida::engines::EngineType::Wavetable: return std::make_unique<dida::engines::WavetableEngine>();
                case dida::engines::EngineType::Granular:  return std::make_unique<dida::engines::GranularEngine>();
                // AI Texture v0.1 — cached neural texture playback.
                case dida::engines::EngineType::NeuralTextureCached:
                    return std::make_unique<dida::engines::NeuralTextureEngine>();
                case dida::engines::EngineType::Pcm:
                default:                                   return std::make_unique<dida::engines::PcmEngine>();
            }
        };

        // Unknown engineType remains non-fatal — falls back to PCM with a log.
        for (auto& pb : p.partials)
        {
            if (pb.engineType.isNotEmpty()
                && dida::engines::engineTypeFromString(pb.engineType) == dida::engines::EngineType::Pcm
                && ! pb.engineType.equalsIgnoreCase("pcm"))
            {
                didaUserPresetLog("WARNING unknown partial engineType=\"" + pb.engineType
                                  + "\" — falling back to pcm (non-fatal)");
            }
        }

        dp->getSynthEngine().forEachSynthVoice([&](SynthVoice& v)
        {
            v.clearPartials();
            if (choirMode)
            {
                // Choir mode clamps the main synth oscillators (unison/exciter/
                // spread) for a natural vocal blend, but the cached AI Texture
                // layer must STILL be installed — otherwise a choir preset with a
                // neuralTextureCached partial reports activePartials: 0 even though
                // the texture exists. We therefore apply the clamps and then fall
                // through to install ONLY neural texture partials below.
                v.setUnisonRender(1, 0.0f, 0.0f, 0.0f);
                v.setExciterAmount(0.0f);
                v.setStereoSpreadAmount(0.0f);
            }

            const int count = juce::jmin((int) SynthVoice::kMaxPartials, p.partials.size());
            for (int i = 0; i < count; ++i)
            {
                const auto& pb = p.partials.getReference(i);

                const bool isNeural =
                    dida::engines::engineTypeFromString(pb.engineType)
                        == dida::engines::EngineType::NeuralTextureCached;

                // In choir mode only the neural texture partial is installed; the
                // synth-support partials are intentionally muted by the choir path.
                // AI Texture presets keep their support/body partial even in
                // choir mode — otherwise the cached-texture choir preset renders
                // texture-only and reports TOO_QUIET. Natural (non-AI) choir
                // presets remain sample-only as before.
                if (choirMode && ! isNeural && ! isAiTexturePreset(p)) continue;

                auto eng = makeEngine(pb.engineType);
                if (eng == nullptr) continue;

                if (isNeural)
                {
                    // ---- AI Texture v0.1 cached configuration + gain safety ----
                    auto* nte = static_cast<dida::engines::NeuralTextureEngine*>(eng.get());
                    const juce::var ep = pb.engineParams;
                    const juce::String texPath = ep.getProperty("texturePath", "").toString();
                    auto& cached = resolveTexture(texPath);
                    if (! cached.missing && cached.buffer != nullptr)
                        nte->setSharedTexture(cached.buffer, cached.sampleRate);

                    nte->setLoop((bool)  ep.getProperty("loop", true));
                    nte->setRootMidi((int) ep.getProperty("rootMidi", 60));
                    nte->setPitchTracking((bool) ep.getProperty("pitchTracking", false));
                    nte->setFollowMainEnvelope(pb.followMainEnvelope);
                    nte->setReleaseMs(pb.amp.releaseMs);
                    nte->setEqRole(pb.eqRole.isNotEmpty() ? pb.eqRole : juce::String("neuralTexture"));
                    nte->setDebugName(p.presetName + "/p" + juce::String(i));

                    // Gain safety: default quiet, hard cap at -9 dB (enforced by
                    // the engine). Priority: engineParams.levelDb, then the
                    // demo-pack top-level partial levelDb, then amp.gainDb.
                    float levelDb = dida::engines::NeuralTextureEngine::kDefaultLevelDb;
                    if (ep.hasProperty("levelDb")) levelDb = (float) (double) ep.getProperty("levelDb", levelDb);
                    else if (pb.hasLevelDb)        levelDb = pb.levelDb;
                    else if (pb.amp.gainDb != 0.0f) levelDb = pb.amp.gainDb;
                    nte->setLevelDb(levelDb);

                    // Neural texture gain is fully owned by the engine; the slot
                    // applies only pan + the live "Texture Amount" multiplier.
                    v.setPartial(i, std::move(eng), pb.enabled, 1.0f, pb.pan,
                                 pb.pitchSemis, pb.fineCents, /*isNeuralTexture=*/true);
                }
                else
                {
                    // Demo-pack support/body partials declare their level in dB
                    // ("levelDb"). Honour it so the support body sits at the
                    // intended level instead of full-scale; fall back to the
                    // legacy linear "level" (default 1.0) when absent.
                    float partialLevel = pb.level;
                    if (pb.hasLevelDb)
                        partialLevel = juce::Decibels::decibelsToGain(pb.levelDb);
                    v.setPartial(i, std::move(eng), pb.enabled, partialLevel, pb.pan,
                                 pb.pitchSemis, pb.fineCents);
                }
            }
        });
    }
}


juce::String toJson(const UserPreset& p)
{
    auto obj = new juce::DynamicObject();
    obj->setProperty("schemaVersion", p.schemaVersion);
    obj->setProperty("presetName",    p.presetName);
    obj->setProperty("category",      p.category);

    auto src = new juce::DynamicObject();
    src->setProperty("type",        p.source.type);
    src->setProperty("path",        p.source.path);
    src->setProperty("mappingMode", p.source.mappingMode);
    juce::Array<juce::var> rn; for (auto& s : p.source.rootNotePattern) rn.add(s);
    src->setProperty("rootNotePattern", rn);
    obj->setProperty("sourceInstrument", juce::var(src));

    auto amp = new juce::DynamicObject();
    amp->setProperty("gainDb", p.amp.gainDb); amp->setProperty("pan", p.amp.pan);
    amp->setProperty("attackMs", p.amp.attackMs); amp->setProperty("decayMs", p.amp.decayMs);
    amp->setProperty("sustain", p.amp.sustain); amp->setProperty("releaseMs", p.amp.releaseMs);
    obj->setProperty("amp", juce::var(amp));

    auto flt = new juce::DynamicObject();
    flt->setProperty("enabled", p.filter.enabled); flt->setProperty("type", p.filter.type);
    flt->setProperty("cutoffHz", p.filter.cutoffHz); flt->setProperty("resonance", p.filter.resonance);
    flt->setProperty("drive", p.filter.drive); flt->setProperty("keytrack", p.filter.keytrack);
    obj->setProperty("filter", juce::var(flt));

    auto layerToVar = [](const LayerBlock& l) {
        auto* o = new juce::DynamicObject();
        o->setProperty("enabled", l.enabled); o->setProperty("gainDb", l.gainDb);
        o->setProperty("pan", l.pan); o->setProperty("octave", l.octave);
        o->setProperty("semitone", l.semitone); o->setProperty("detuneCents", l.detuneCents);
        if (l.blendMode.isNotEmpty()) o->setProperty("blendMode", l.blendMode);
        if (l.eqRole.isNotEmpty())    o->setProperty("eqRole",    l.eqRole);
        o->setProperty("followMainEnvelope", l.followMainEnvelope);
        if (l.maxGainDb != 0.0f)      o->setProperty("maxGainDb", l.maxGainDb);
        return juce::var(o);
    };
    auto layers = new juce::DynamicObject();
    layers->setProperty("main",   layerToVar(p.main));
    layers->setProperty("layer2", layerToVar(p.layer2));
    obj->setProperty("layers", juce::var(layers));

    auto fx = new juce::DynamicObject();
    auto cho = new juce::DynamicObject();
    cho->setProperty("enabled", p.chorus.enabled); cho->setProperty("rateHz", p.chorus.rateHz);
    cho->setProperty("depth", p.chorus.depth); cho->setProperty("mix", p.chorus.mix);
    fx->setProperty("chorus", juce::var(cho));
    auto del = new juce::DynamicObject();
    del->setProperty("enabled", p.delay.enabled); del->setProperty("timeMs", p.delay.timeMs);
    del->setProperty("feedback", p.delay.feedback); del->setProperty("mix", p.delay.mix);
    fx->setProperty("delay", juce::var(del));
    auto rev = new juce::DynamicObject();
    rev->setProperty("enabled", p.reverb.enabled); rev->setProperty("size", p.reverb.size);
    rev->setProperty("damping", p.reverb.damping); rev->setProperty("mix", p.reverb.mix);
    fx->setProperty("reverb", juce::var(rev));
    auto sat = new juce::DynamicObject();
    sat->setProperty("enabled", p.saturation.enabled); sat->setProperty("drive", p.saturation.drive);
    sat->setProperty("mix", p.saturation.mix);
    fx->setProperty("saturation", juce::var(sat));
    obj->setProperty("fx", juce::var(fx));

    auto fxSend = new juce::DynamicObject();
    if (p.fxSend.hasFxSendReleaseMs) fxSend->setProperty("fxSendReleaseMs", p.fxSend.fxSendReleaseMs);
    if (p.fxSend.hasFxSendMaximumReleaseMs) fxSend->setProperty("fxSendMaximumReleaseMs", p.fxSend.fxSendMaximumReleaseMs);
    if (p.fxSend.hasFxSendReleaseMultiplier) fxSend->setProperty("fxSendReleaseMultiplier", p.fxSend.fxSendReleaseMultiplier);
    fxSend->setProperty("noteOffStopsFxSend", p.fxSend.noteOffStopsFxSend);
    obj->setProperty("fxSend", juce::var(fxSend));

    if (p.choirMode) obj->setProperty("choirMode", true);
    if (p.safety.hasChoirFxSendReleaseMaxMs)
    {
        auto safety = new juce::DynamicObject();
        safety->setProperty("choirFxSendReleaseMaxMs", p.safety.choirFxSendReleaseMaxMs);
        obj->setProperty("safety", juce::var(safety));
    }

    auto lfoToVar = [](const LfoBlock& l) {
        auto* o = new juce::DynamicObject();
        o->setProperty("enabled", l.enabled); o->setProperty("target", l.target);
        o->setProperty("shape", l.shape); o->setProperty("rateHz", l.rateHz);
        o->setProperty("depth", l.depth);
        return juce::var(o);
    };
    auto mod = new juce::DynamicObject();
    mod->setProperty("lfo1", lfoToVar(p.lfo1));
    mod->setProperty("lfo2", lfoToVar(p.lfo2));
    obj->setProperty("modulation", juce::var(mod));

    auto adv = new juce::DynamicObject();
    adv->setProperty("sampleStartMs", p.advanced.sampleStartMs);
    adv->setProperty("randomStartMs", p.advanced.randomStartMs);
    adv->setProperty("velocityToGain", p.advanced.velocityToGain);
    adv->setProperty("velocityToCutoff", p.advanced.velocityToCutoff);
    adv->setProperty("humanizePitchCents", p.advanced.humanizePitchCents);
    adv->setProperty("humanizeTimingMs", p.advanced.humanizeTimingMs);
    adv->setProperty("polyphony", p.advanced.polyphony);
    obj->setProperty("advanced", juce::var(adv));

    // v2 blocks
    auto mac = new juce::DynamicObject();
    mac->setProperty("tone",      p.macros.tone);
    mac->setProperty("movement",  p.macros.movement);
    mac->setProperty("width",     p.macros.width);
    mac->setProperty("warmth",    p.macros.warmth);
    mac->setProperty("attack",    p.macros.attack);
    mac->setProperty("release",   p.macros.release);
    mac->setProperty("space",     p.macros.space);
    mac->setProperty("character", p.macros.character);
    obj->setProperty("macros", juce::var(mac));

    auto vel = new juce::DynamicObject();
    vel->setProperty("toGain",       p.velocity.toGain);
    vel->setProperty("toCutoff",     p.velocity.toCutoff);
    vel->setProperty("toAttack",     p.velocity.toAttack);
    vel->setProperty("toLayerBlend", p.velocity.toLayerBlend);
    obj->setProperty("velocity", juce::var(vel));

    auto leq = new juce::DynamicObject();
    leq->setProperty("mainBodyHz",   p.layerEq.mainBodyHz);
    leq->setProperty("mainAirHz",    p.layerEq.mainAirHz);
    leq->setProperty("layer2BodyHz", p.layerEq.layer2BodyHz);
    leq->setProperty("layer2AirHz",  p.layerEq.layer2AirHz);
    obj->setProperty("layerEq", juce::var(leq));

    auto fm = new juce::DynamicObject();
    fm->setProperty("enabled", p.filterMovement.enabled);
    fm->setProperty("depth",   p.filterMovement.depth);
    fm->setProperty("rateHz",  p.filterMovement.rateHz);
    obj->setProperty("filterMovement", juce::var(fm));

    obj->setProperty("experimental", p.experimental);

    // Mod-matrix routings — serialized as an array of small objects so user
    // hand-edits remain readable.
    juce::Array<juce::var> mm;
    for (auto& e : p.modMatrix)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("source",  e.source);
        o->setProperty("dest",    e.dest);
        o->setProperty("amount",  e.amount);
        o->setProperty("bipolar", e.bipolar);
        mm.add(juce::var(o));
    }
    obj->setProperty("modMatrix", mm);

    // ------- Partials (v2 additive). Omitted when empty so existing
    //         .diapreset files round-trip byte-identically. -------
    if (p.engineType.isNotEmpty())
        obj->setProperty("engineType", p.engineType);

    if (! p.partials.isEmpty())
    {
        juce::Array<juce::var> ps;
        for (auto& pb : p.partials)
        {
            auto* po = new juce::DynamicObject();
            po->setProperty("enabled",    pb.enabled);
            po->setProperty("engineType", pb.engineType);
            po->setProperty("level",      pb.level);
            po->setProperty("pan",        pb.pan);
            po->setProperty("pitchSemis", pb.pitchSemis);
            po->setProperty("fineCents",  pb.fineCents);
            if (pb.blendMode.isNotEmpty()) po->setProperty("blendMode", pb.blendMode);
            if (pb.eqRole.isNotEmpty())    po->setProperty("eqRole",    pb.eqRole);
            po->setProperty("followMainEnvelope", pb.followMainEnvelope);
            if (pb.maxGainDb != 0.0f)      po->setProperty("maxGainDb", pb.maxGainDb);
            if (! pb.engineParams.isVoid())
                po->setProperty("engineParams", pb.engineParams);

            auto* pa = new juce::DynamicObject();
            pa->setProperty("gainDb", pb.amp.gainDb); pa->setProperty("pan", pb.amp.pan);
            pa->setProperty("attackMs", pb.amp.attackMs); pa->setProperty("decayMs", pb.amp.decayMs);
            pa->setProperty("sustain", pb.amp.sustain); pa->setProperty("releaseMs", pb.amp.releaseMs);
            po->setProperty("amp", juce::var(pa));

            auto* pf = new juce::DynamicObject();
            pf->setProperty("enabled", pb.filter.enabled); pf->setProperty("type", pb.filter.type);
            pf->setProperty("cutoffHz", pb.filter.cutoffHz); pf->setProperty("resonance", pb.filter.resonance);
            pf->setProperty("drive", pb.filter.drive); pf->setProperty("keytrack", pb.filter.keytrack);
            po->setProperty("filter", juce::var(pf));

            auto* pl = new juce::DynamicObject();
            pl->setProperty("enabled", pb.lfo.enabled); pl->setProperty("target", pb.lfo.target);
            pl->setProperty("shape", pb.lfo.shape); pl->setProperty("rateHz", pb.lfo.rateHz);
            pl->setProperty("depth", pb.lfo.depth);
            po->setProperty("lfo", juce::var(pl));

            juce::Array<juce::var> pmods;
            for (auto& e : pb.mods)
            {
                auto* eo = new juce::DynamicObject();
                eo->setProperty("source",  e.source);
                eo->setProperty("dest",    e.dest);
                eo->setProperty("amount",  e.amount);
                eo->setProperty("bipolar", e.bipolar);
                pmods.add(juce::var(eo));
            }
            po->setProperty("mods", pmods);

            ps.add(juce::var(po));
        }
        obj->setProperty("partials", ps);
    }

    // ------- AI Texture v0.1 metadata. Only serialized when present so existing
    //         .diapreset files round-trip byte-identically. -------
    if (p.ai.present || p.ai.enabled)
    {
        auto* aio = new juce::DynamicObject();
        aio->setProperty("enabled",        p.ai.enabled);
        aio->setProperty("profileVersion", p.ai.profileVersion);
        if (p.ai.provider.isNotEmpty())     aio->setProperty("provider",     p.ai.provider);
        if (p.ai.analysisFile.isNotEmpty()) aio->setProperty("analysisFile", p.ai.analysisFile);
        aio->setProperty("textureMode",    p.ai.textureMode);

        auto* tp = new juce::DynamicObject();
        tp->setProperty("brightness",       p.ai.timbreProfile.brightness);
        tp->setProperty("harmonicDensity",  p.ai.timbreProfile.harmonicDensity);
        tp->setProperty("noiseAir",         p.ai.timbreProfile.noiseAir);
        tp->setProperty("attackNoise",      p.ai.timbreProfile.attackNoise);
        tp->setProperty("pitchInstability", p.ai.timbreProfile.pitchInstability);
        tp->setProperty("bodyWarmth",       p.ai.timbreProfile.bodyWarmth);
        aio->setProperty("timbreProfile", juce::var(tp));

        obj->setProperty("ai", juce::var(aio));
    }

    return juce::JSON::toString(juce::var(obj));
}

}} // namespace dida::userpreset
