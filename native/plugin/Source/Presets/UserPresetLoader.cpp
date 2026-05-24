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
    switch (f) {
        case Family::PianoKeys: return { 0.18f, 0.16f, 0.32f, 0.12f };
        case Family::Lead:      return { 0.35f, 0.45f, 0.50f, 0.45f };
        case Family::Pad:       return { 0.45f, 0.40f, 0.55f, 0.25f };
        case Family::ChoirVox:  return { 0.35f, 0.30f, 0.55f, 0.18f };
        case Family::Brass:     return { 0.25f, 0.30f, 0.42f, 0.30f };
        case Family::Guitar:    return { 0.30f, 0.35f, 0.38f, 0.40f };
        case Family::Bell:      return { 0.30f, 0.35f, 0.48f, 0.18f };
        case Family::Pluck:     return { 0.30f, 0.35f, 0.35f, 0.18f };
        case Family::Bass808:   return { 0.12f, 0.18f, 0.18f, 0.35f };
        case Family::FxRiser:   return { 0.60f, 0.70f, 0.70f, 0.60f };
        // Vintage synth strict caps (chorus/delay/reverb/sat) — matches the
        // "remove static, keep warm" spec. Saturation drive is also capped
        // hard at 0.16 to stop layer-bus tanh from overdriving the chain.
        case Family::Synth:     return { 0.18f, 0.10f, 0.18f, 0.16f };
        default:                return { 0.50f, 0.50f, 0.50f, 0.40f };
    }
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

    bus.setEnabled(true);
    bus.setSaturationDrive(0.08f + warmth * 0.30f);   // subtle analog glue
    bus.setSaturationMix  (0.10f + warmth * 0.35f);

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

void applyToProcessor(const UserPreset& p, juce::AudioProcessor& proc)
{
    const Family fam = familyOf(p.category);

    // -- Source / category validation (non-fatal warning only)
    validateSourceForCategory(p);

    // -- Master gain
    setParamById(proc, "masterGain", p.amp.gainDb);

    // -- Amp env: clamped to per-category musical ranges
    clampEnvelopeForCategory(proc, p, fam);

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

    // -- FX: clamp each mix to a category-sane upper limit so e.g. piano
    //        presets cannot ship as a wet-bath. Macro "space" can nudge
    //        reverb mix up to the cap.
    const auto fxL = fxLimitsFor(fam);
    const float space   = juce::jlimit(0.0f, 1.0f, p.macros.space);
    const float reverbBase = p.reverb.enabled ? p.reverb.mix : 0.0f;
    const float reverbMix  = juce::jlimit(0.0f, fxL.reverbMax,
                                          reverbBase * (0.85f + space * 0.30f));

    setParamById(proc, "fxChorusMix",
                 juce::jlimit(0.0f, fxL.chorusMax, p.chorus.enabled ? p.chorus.mix : 0.0f));
    setParamById(proc, "fxDelayMix",
                 juce::jlimit(0.0f, fxL.delayMax,  p.delay.enabled  ? p.delay.mix  : 0.0f));
    setParamById(proc, "fxDelayTime",        p.delay.timeMs / 1000.0f);
    setParamById(proc, "fxDelayFeedback",    p.delay.feedback);
    setParamById(proc, "fxReverbMix",        reverbMix);
    setParamById(proc, "fxReverbSize",       p.reverb.size);
    setParamById(proc, "fxDistortionAmount",
                 juce::jlimit(0.0f, fxL.satMax,    p.saturation.enabled ? p.saturation.drive : 0.0f));

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

    return juce::JSON::toString(juce::var(obj));
}

}} // namespace dida::userpreset
