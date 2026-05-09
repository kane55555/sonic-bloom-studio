#include "HybridPresetGenerator.h"
#include "PresetTemplateFactory.h"

namespace dida { namespace preset {

HybridPresetV2 HybridPresetGenerator::generate(const Inputs& in)
{
    auto p = PresetTemplateFactory::build(in.category, in.presetName,
                                          in.sampleRelPath, in.rootMidi, in.rootNote);
    if (! p.layers.empty())
    {
        auto& L = p.layers[0];
        L.source        = in.sampleRelPath;
        L.rootNote      = in.rootNote;
        L.rootMidi      = in.rootMidi;
        L.volume        = 1.0f;
        L.pitchTracking = in.pitchTracking;
        L.oneShotMode   = in.oneShotMode;
    }
    for (size_t i = 1; i < p.layers.size(); ++i)
    {
        p.layers[i].enabled = false;
        p.layers[i].volume = 0.0f;
    }
    p.hasSourceImport         = true;
    p.sourceOriginalFileName  = in.originalFileName;
    p.sourceSamplePath        = in.sampleRelPath;
    p.sourceRootNote          = in.rootNote;
    p.sourceRootMidi          = in.rootMidi;
    p.sourceRootSrc           = in.rootNoteSource;
    p.sourcePitchTracking     = in.pitchTracking;
    p.needsReview             = in.needsReview;
    p.rootNoteVerified        = (in.rootNoteSource == "filename" || in.rootNoteSource == "manual");
    p.dateCreated = p.dateModified =
        juce::Time::getCurrentTime().toISO8601(true);
    p.presetId = juce::Uuid().toString();
    return p;
}

static juce::var envToVar(const AmpEnvV2& e)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("attack", e.attack);
    o->setProperty("decay", e.decay);
    o->setProperty("sustain", e.sustain);
    o->setProperty("release", e.release);
    return juce::var(o.get());
}

static juce::var filterToVar(const LayerFilterV2& f)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("enabled", f.enabled);
    o->setProperty("type", f.type);
    o->setProperty("cutoff", f.cutoff);
    o->setProperty("resonance", f.resonance);
    o->setProperty("drive", f.drive);
    return juce::var(o.get());
}

static juce::var layerToVar(const LayerV2& L)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("id", L.id);
    o->setProperty("name", L.name);
    const char* t = "sample";
    switch (L.type) {
        case LayerType::Sample:     t = "sample"; break;
        case LayerType::Oscillator: t = "oscillator"; break;
        case LayerType::Noise:      t = "noise"; break;
        case LayerType::Texture:    t = "texture"; break;
    }
    o->setProperty("type", t);
    o->setProperty("enabled", L.enabled);
    o->setProperty("volume", L.volume);
    o->setProperty("pan", L.pan);
    o->setProperty("ampEnvelope", envToVar(L.ampEnv));
    if (L.hasFilter) o->setProperty("filter", filterToVar(L.filter));
    if (L.type == LayerType::Sample || L.type == LayerType::Texture) {
        o->setProperty("source", L.source);
        o->setProperty("rootNote", L.rootNote);
        o->setProperty("rootMidi", L.rootMidi);
        o->setProperty("pitchTracking", L.pitchTracking);
        o->setProperty("oneShotMode", L.oneShotMode);
        o->setProperty("pitch", L.pitchSemis);
        o->setProperty("fineTune", L.fineCents);
        o->setProperty("reverse", L.reverse);
        o->setProperty("loop", L.loop);
    }
    if (L.type == LayerType::Oscillator) {
        o->setProperty("waveform", L.waveform);
        o->setProperty("pitch", L.pitchSemis);
        o->setProperty("fineTune", L.fineCents);
    }
    return juce::var(o.get());
}

static juce::var effectsToVar(const EffectsV2& fx)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    auto sub = [](std::initializer_list<std::pair<const char*, juce::var>> kv) {
        juce::DynamicObject::Ptr d = new juce::DynamicObject();
        for (auto& p : kv) d->setProperty(p.first, p.second);
        return juce::var(d.get());
    };
    o->setProperty("eq", sub({{"enabled",fx.eqEnabled},{"lowCut",fx.eqLowCut},
        {"body",fx.eqBody},{"presence",fx.eqPresence},{"air",fx.eqAir}}));
    o->setProperty("saturation", sub({{"enabled",fx.satEnabled},{"mode",fx.satMode},
        {"drive",fx.satDrive},{"mix",fx.satMix}}));
    o->setProperty("chorus", sub({{"enabled",fx.chorusEnabled},{"rate",fx.chorusRate},
        {"depth",fx.chorusDepth},{"mix",fx.chorusMix}}));
    o->setProperty("delay", sub({{"enabled",fx.delayEnabled},{"sync",fx.delaySync},
        {"time",fx.delayTime},{"feedback",fx.delayFb},{"mix",fx.delayMix}}));
    o->setProperty("reverb", sub({{"enabled",fx.reverbEnabled},{"size",fx.reverbSize},
        {"decay",fx.reverbDecay},{"mix",fx.reverbMix}}));
    o->setProperty("width", sub({{"enabled",fx.widthEnabled},{"amount",fx.widthAmount}}));
    o->setProperty("limiter", sub({{"enabled",fx.limiterEnabled},{"ceiling",fx.limiterCeiling}}));
    return juce::var(o.get());
}

juce::String HybridPresetGenerator::toJsonString(const HybridPresetV2& p)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("schemaVersion", p.schemaVersion);
    o->setProperty("plugin", "DIDITAGAIN STUDIO");
    o->setProperty("presetId", p.presetId);
    o->setProperty("name", p.name);
    o->setProperty("bank", p.bank);
    o->setProperty("category", p.category);
    if (p.subCategory.isNotEmpty()) o->setProperty("subCategory", p.subCategory);
    o->setProperty("author", p.author);
    o->setProperty("dateCreated", p.dateCreated);
    o->setProperty("dateModified", p.dateModified);
    juce::Array<juce::var> tagsArr;
    for (auto& t : p.tags) tagsArr.add(t);
    o->setProperty("tags", tagsArr);
    o->setProperty("engine", "hybrid");

    if (p.hasSourceImport) {
        juce::DynamicObject::Ptr s = new juce::DynamicObject();
        s->setProperty("originalFileName", p.sourceOriginalFileName);
        s->setProperty("samplePath", p.sourceSamplePath);
        s->setProperty("detectedRootNote", p.sourceRootNote);
        s->setProperty("rootMidi", p.sourceRootMidi);
        s->setProperty("rootNoteSource", p.sourceRootSrc);
        s->setProperty("pitchTracking", p.sourcePitchTracking);
        o->setProperty("sourceImport", juce::var(s.get()));
    }

    juce::DynamicObject::Ptr q = new juce::DynamicObject();
    q->setProperty("needsReview", p.needsReview);
    q->setProperty("rootNoteVerified", p.rootNoteVerified);
    o->setProperty("quality", juce::var(q.get()));

    juce::Array<juce::var> layers;
    for (auto& L : p.layers) layers.add(layerToVar(L));
    o->setProperty("layers", layers);

    o->setProperty("globalFilter", filterToVar(p.globalFilter));
    o->setProperty("effects", effectsToVar(p.effects));

    juce::Array<juce::var> macros;
    for (auto& m : p.macros) {
        juce::DynamicObject::Ptr mo = new juce::DynamicObject();
        mo->setProperty("id", m.id); mo->setProperty("name", m.name);
        mo->setProperty("value", m.value);
        juce::Array<juce::var> ts;
        for (auto& t : m.targets) {
            juce::DynamicObject::Ptr to = new juce::DynamicObject();
            to->setProperty("path", t.path);
            to->setProperty("min", t.min); to->setProperty("max", t.max);
            ts.add(juce::var(to.get()));
        }
        mo->setProperty("targets", ts);
        macros.add(juce::var(mo.get()));
    }
    o->setProperty("macros", macros);

    return juce::JSON::toString(juce::var(o.get()), /*allOnOneLine*/ false);
}

}} // namespace dida::preset
