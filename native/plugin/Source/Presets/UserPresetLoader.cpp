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
#include <algorithm>

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

    out.presetName = getS(json, "presetName", {});
    if (out.presetName.isEmpty())
    {
        errorOut = "missing presetName";
        return false;
    }
    out.category = getS(json, "category", "User");

    auto src = json.getProperty("sourceInstrument", juce::var());
    if (! src.isObject())
    {
        errorOut = "missing sourceInstrument";
        return false;
    }
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

    auto amp = json.getProperty("amp", juce::var());
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
    out.filter.keytrack  = getF(flt, "keytrack",  out.filter.keytrack);

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
    out.delay.feedback = getF(del, "feedback", out.delay.feedback);
    out.delay.mix      = getF(del, "mix",      out.delay.mix);

    auto rev = fx.getProperty("reverb", juce::var());
    out.reverb.enabled = getB(rev, "enabled", false);
    out.reverb.size    = getF(rev, "size",    out.reverb.size);
    out.reverb.damping = getF(rev, "damping", out.reverb.damping);
    out.reverb.mix     = getF(rev, "mix",     out.reverb.mix);

    auto sat = fx.getProperty("saturation", juce::var());
    out.saturation.enabled = getB(sat, "enabled", false);
    out.saturation.drive   = getF(sat, "drive",   out.saturation.drive);
    out.saturation.mix     = getF(sat, "mix",     out.saturation.mix);

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

    return true;
}

static bool folderHasWavs(const juce::File& folder)
{
    return folder.isDirectory()
        && ! folder.findChildFiles(juce::File::findFiles, false, "*.wav").isEmpty();
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
    auto src = rawPath.replaceCharacter('\\', '/').trim().trimCharactersAtStartAndEnd("\"'");
    if (src.isEmpty()) return {};

    auto samplesRoot = dida::SampleLibrary::getSamplesRoot();
    auto docsRoot = samplesRoot.getParentDirectory();
    juce::Array<juce::File> candidates;

    auto expanded = src;
    expanded = expanded.replace("{DocsRoot}", docsRoot.getFullPathName().replaceCharacter('\\', '/'), true);
    expanded = expanded.replace("{Docs}",     docsRoot.getFullPathName().replaceCharacter('\\', '/'), true);
    expanded = expanded.replace("{Documents}", docsRoot.getFullPathName().replaceCharacter('\\', '/'), true);
    expanded = expanded.replace("{Samples}",  samplesRoot.getFullPathName().replaceCharacter('\\', '/'), true);

    if (juce::File::isAbsolutePath(expanded))
        addCandidate(candidates, juce::File(expanded));

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

    for (auto& candidate : candidates)
        if (candidate.isDirectory())
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

    return candidates.isEmpty() ? juce::File(expanded) : candidates.getFirst();
}

// ---- APVTS setters (mirror PresetManager.cpp helpers) ---------------------
static void setParamRaw(juce::AudioProcessorParameter* p, float normalised)
{
    if (p != nullptr) p->setValue(juce::jlimit(0.0f, 1.0f, normalised));
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

void applyToProcessor(const UserPreset& p, juce::AudioProcessor& proc)
{
    // -- Amp / envelope 1 (master amp env)
    setParamById(proc, "masterGain",   p.amp.gainDb);
    setParamById(proc, "env1Attack",   p.amp.attackMs   / 1000.0f);
    setParamById(proc, "env1Decay",    p.amp.decayMs    / 1000.0f);
    setParamById(proc, "env1Sustain",  juce::jlimit(0.0f, 1.0f, p.amp.sustain));
    setParamById(proc, "env1Release",  p.amp.releaseMs  / 1000.0f);

    // -- Filter
    setChoiceById(proc, "filter1Type", filterTypeIndex(p.filter.type));
    setParamById(proc, "filter1Cutoff",    p.filter.enabled ? p.filter.cutoffHz : 20000.0f);
    setParamById(proc, "filter1Resonance", p.filter.resonance);
    setParamById(proc, "filter1Drive",     p.filter.drive);
    setParamById(proc, "filter1KeyTrack",  p.filter.keytrack);

    // -- Layer 2 (octave/detune routed onto Osc B controls)
    setParamById(proc, "oscBLevel",  p.layer2.enabled
        ? juce::Decibels::decibelsToGain(p.layer2.gainDb) : 0.0f);
    setParamById(proc, "oscBOctave", static_cast<float>(p.layer2.octave));
    setParamById(proc, "oscBSemi",   static_cast<float>(p.layer2.semitone));
    setParamById(proc, "oscBDetune", p.layer2.detuneCents);

    // Sample is the only sound source — make sure Osc A pass-through is on,
    // sub/noise silenced. (Same convention as PresetManager sample-drop path.)
    setParamById(proc, "oscALevel",    1.0f);
    setParamById(proc, "subOscLevel",  0.0f);
    setParamById(proc, "noiseLevel",   0.0f);
    for (auto* param : proc.getParameters())
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(param))
            if (b->paramID == "subOscEnabled") setParamRaw(b, 0.0f);

    // -- FX
    setParamById(proc, "fxChorusMix",        p.chorus.enabled    ? p.chorus.mix     : 0.0f);
    setParamById(proc, "fxDelayMix",         p.delay.enabled     ? p.delay.mix      : 0.0f);
    setParamById(proc, "fxDelayTime",        p.delay.timeMs / 1000.0f);
    setParamById(proc, "fxDelayFeedback",    p.delay.feedback);
    setParamById(proc, "fxReverbMix",        p.reverb.enabled    ? p.reverb.mix     : 0.0f);
    setParamById(proc, "fxReverbSize",       p.reverb.size);
    setParamById(proc, "fxDistortionAmount", p.saturation.enabled ? p.saturation.drive : 0.0f);

    // -- LFOs (rate + shape; routing matrix is owned by the engine elsewhere)
    setParamById(proc, "lfo1Rate", p.lfo1.rateHz);
    setChoiceById(proc, "lfo1Shape", lfoShapeIndex(p.lfo1.shape));
    setParamById(proc, "lfo2Rate", p.lfo2.rateHz);
    setChoiceById(proc, "lfo2Shape", lfoShapeIndex(p.lfo2.shape));

    // -- Advanced
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
        + " reverbMix=" + juce::String(p.reverb.mix, 2)
        + " delayMix=" + juce::String(p.delay.mix, 2)
        + " chorusMix=" + juce::String(p.chorus.mix, 2));
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

    return juce::JSON::toString(juce::var(obj));
}

}} // namespace dida::userpreset
