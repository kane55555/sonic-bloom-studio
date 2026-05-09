#include "HybridPresetApplier.h"

namespace dida { namespace preset {

// ---- shared helpers (mirror of PresetManager.cpp ones; kept local to avoid linkage churn) ----

static void setRaw(juce::AudioProcessorParameter* p, float n)
{
    if (p != nullptr) p->setValue(juce::jlimit(0.0f, 1.0f, n));
}

static juce::AudioProcessorParameterWithID* findParam(juce::AudioProcessor& proc, const char* id)
{
    for (auto* p : proc.getParameters())
        if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            if (w->paramID == id)
                return w;
    return nullptr;
}

static void setFloat(juce::AudioProcessor& proc, const char* id, float v)
{
    if (auto* p = findParam(proc, id))
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
            setRaw(r, r->convertTo0to1(v));
}

static void setBool(juce::AudioProcessor& proc, const char* id, bool b)
{
    if (auto* p = findParam(proc, id))
        setRaw(p, b ? 1.0f : 0.0f);
}

static void setChoice(juce::AudioProcessor& proc, const char* id, const juce::String& label)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(findParam(proc, id)))
    {
        const int idx = c->choices.indexOf(label, true);
        if (idx >= 0) setRaw(c, c->convertTo0to1((float) idx));
    }
}

static juce::String waveformToChoiceLabel(const juce::String& w)
{
    auto s = w.toLowerCase();
    if (s == "sine")        return "Sine";
    if (s == "triangle")    return "Triangle";
    if (s == "saw")         return "Saw";
    if (s == "square")      return "Square";
    if (s == "pulse")       return "Pulse";
    if (s == "supersaw")    return "SuperSaw";
    if (s == "fmcarrier")   return "FmCarrier";
    if (s == "wavetable")   return "Wavetable";
    return "Sine";
}

static juce::String filterTypeToChoice(const juce::String& t)
{
    auto s = t.toLowerCase();
    if (s == "lowpass" || s == "lp24") return "LP24";
    if (s == "lp12")                   return "LP12";
    if (s == "highpass"|| s == "hp24") return "HP24";
    if (s == "hp12")                   return "HP12";
    if (s == "bandpass"|| s == "bp")   return "BP";
    if (s == "notch")                  return "Notch";
    return "LP24";
}

bool HybridPresetApplier::shouldLoopForCategory(const juce::String& category,
                                                bool oneShotMode,
                                                bool layerLoop)
{
    if (oneShotMode) return false;
    if (layerLoop)   return true;

    const auto c = category;
    if (c == "DarkPads" || c == "Textures") return true;
    if (c == "ChoirsVox")                    return true;  // sustained vocal pad behavior
    if (c == "AlienLeads")                   return true;
    if (c == "Bass808")                      return false; // unless layerLoop
    if (c == "DrillBells" || c == "Plucks" || c == "PainPianos" || c == "Guitars") return false;
    if (c == "FXRisers")                     return false;
    return false;
}

AppliedPresetState HybridPresetApplier::apply(const HybridPresetV2& p,
                                              juce::AudioProcessor& processor)
{
    AppliedPresetState out;
    out.preset   = p;
    out.category = p.category;

    // ---- Locate logical layers ----
    const LayerV2* sampleLayer = nullptr;
    const LayerV2* bodyLayer   = nullptr;   // layers[1] osc body
    const LayerV2* airLayer    = nullptr;   // layers[2] noise
    const LayerV2* shimmerLayer = nullptr;  // layers[3] shimmer / sub osc

    for (size_t i = 0; i < p.layers.size(); ++i)
    {
        const auto& L = p.layers[i];
        if (L.type == LayerType::Sample && sampleLayer == nullptr) sampleLayer = &L;
        else if (L.type == LayerType::Oscillator && bodyLayer == nullptr) bodyLayer = &L;
        else if (L.type == LayerType::Noise && airLayer == nullptr)       airLayer = &L;
        else if (L.type == LayerType::Oscillator && shimmerLayer == nullptr) shimmerLayer = &L;
    }

    // ---- Layer 1 sample source ----
    if (sampleLayer != nullptr && sampleLayer->enabled && sampleLayer->source.isNotEmpty())
    {
        out.hasSample       = true;
        out.sampleSource    = sampleLayer->source.replace("\\", "/");
        out.sampleRootMidi  = juce::jlimit(0, 127, sampleLayer->rootMidi);
        out.displayName     = p.name;
        const bool effectiveLoop = sampleLayer->loop || sampleLayer->autoLoop;
        out.shouldLoop      = shouldLoopForCategory(p.category,
                                                    sampleLayer->oneShotMode,
                                                    effectiveLoop);
        out.cropStart       = sampleLayer->cropStart;
        out.cropEnd         = sampleLayer->cropEnd;
        out.loopStart       = sampleLayer->loopStart;
        out.loopEnd         = sampleLayer->loopEnd;
        out.loopCrossfadeMs = sampleLayer->loopCrossfadeMs;
        out.oneShotMode     = sampleLayer->oneShotMode;
        out.pitchTracking   = sampleLayer->pitchTracking;

        // Master amp env follows sample layer.
        setFloat(processor, "env1Attack",  sampleLayer->ampEnv.attack);
        setFloat(processor, "env1Decay",   sampleLayer->ampEnv.decay);
        setFloat(processor, "env1Sustain", sampleLayer->ampEnv.sustain);
        setFloat(processor, "env1Release", sampleLayer->ampEnv.release);
        setFloat(processor, "oscALevel",   sampleLayer->volume);
    }
    else if (p.hasSourceImport && p.sourceSamplePath.isNotEmpty())
    {
        out.hasSample      = true;
        out.sampleSource   = p.sourceSamplePath.replace("\\", "/");
        out.sampleRootMidi = juce::jlimit(0, 127, p.sourceRootMidi);
        out.displayName    = p.name;
        out.shouldLoop     = shouldLoopForCategory(p.category, false, false);
        setFloat(processor, "oscALevel", 1.0f);
    }
    else
    {
        setFloat(processor, "oscALevel", 0.0f);
    }

    // ---- Layer 2 oscillator body -> Osc B ----
    if (bodyLayer != nullptr && bodyLayer->enabled)
    {
        setChoice(processor, "oscBWaveform", waveformToChoiceLabel(bodyLayer->waveform));
        setFloat (processor, "oscBLevel",    bodyLayer->volume);
        const int semi = juce::jlimit(-12, 12, bodyLayer->pitchSemis % 12);
        const int oct  = juce::jlimit(-3, 3, bodyLayer->pitchSemis / 12);
        setFloat (processor, "oscBOctave",   (float) oct);
        setFloat (processor, "oscBSemi",     (float) semi);
        setFloat (processor, "oscBDetune",   (float) bodyLayer->fineCents);
    }
    else
    {
        setFloat(processor, "oscBLevel", 0.0f);
    }

    // ---- Layer 3 noise / air ----
    if (airLayer != nullptr && airLayer->enabled)
        setFloat(processor, "noiseLevel", airLayer->volume);
    else
        setFloat(processor, "noiseLevel", 0.0f);

    // ---- Layer 4 shimmer / sub ----
    if (shimmerLayer != nullptr && shimmerLayer->enabled)
    {
        setBool (processor, "subOscEnabled", true);
        setFloat(processor, "subOscLevel",   shimmerLayer->volume);
    }
    else
    {
        setBool (processor, "subOscEnabled", false);
        setFloat(processor, "subOscLevel",   0.0f);
    }

    // ---- Bass808 special: mono + glide on, otherwise poly ----
    if (p.category == "Bass808")
    {
        out.monoMode = true;
        setBool (processor, "monoMode", true);
        setFloat(processor, "glideTime", 0.05f);
    }
    else
    {
        out.monoMode = false;
        setBool (processor, "monoMode", false);
    }

    // ---- Global filter ----
    setChoice(processor, "filter1Type",      filterTypeToChoice(p.globalFilter.type));
    setFloat (processor, "filter1Cutoff",    p.globalFilter.cutoff);
    setFloat (processor, "filter1Resonance", p.globalFilter.resonance);
    setFloat (processor, "filter1Drive",     p.globalFilter.drive);

    // ---- Effects ----
    setFloat(processor, "fxReverbMix",        p.effects.reverbEnabled ? p.effects.reverbMix : 0.0f);
    setFloat(processor, "fxReverbSize",       p.effects.reverbSize);
    setFloat(processor, "fxDelayMix",         p.effects.delayEnabled  ? p.effects.delayMix  : 0.0f);
    setFloat(processor, "fxDelayFeedback",    p.effects.delayFb);
    setFloat(processor, "fxChorusMix",        p.effects.chorusEnabled ? p.effects.chorusMix : 0.0f);
    setFloat(processor, "fxDistortionAmount", p.effects.satEnabled    ? p.effects.satDrive  : 0.0f);

    return out;
}

}} // namespace dida::preset
