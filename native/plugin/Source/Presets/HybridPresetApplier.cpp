#include "HybridPresetApplier.h"
#include "../PluginProcessor.h"
#include "../DSP/SynthEngine.h"
#include "../DSP/Voice.h"

namespace dida { namespace preset {

// Category tuning profile for the new hybrid-synth DSP. These four numbers
// (unison voices, detune, stereo spread, harmonic exciter) decide whether a
// preset sounds wide and supersaw-y (leads), slow and lush (pads), tight and
// mono (808s), or warm and natural (pianos). They are applied per voice so
// every preset in a category gets the right baseline character without
// having to author per-preset DSP.
struct CategoryDsp
{
    int   unisonVoices = 1;
    float unisonDetune = 0.0f;
    float stereoSpread = 0.0f;
    float exciter      = 0.0f;
    float drift        = 0.25f;
};

static CategoryDsp dspForCategory(const juce::String& categoryIn) noexcept
{
    const auto c = categoryIn.toLowerCase();
    CategoryDsp d;
    if (c.contains("lead") || c.contains("alien"))     { d = { 7, 0.55f, 0.85f, 0.45f, 0.30f }; }
    else if (c.contains("pad") || c.contains("texture") || c.contains("ambient"))
                                                       { d = { 5, 0.35f, 0.90f, 0.25f, 0.55f }; }
    else if (c.contains("choir") || c.contains("vox")) { d = { 4, 0.20f, 0.75f, 0.20f, 0.40f }; }
    else if (c.contains("brass") || c.contains("horn")|| c.contains("trumpet"))
                                                       { d = { 3, 0.18f, 0.55f, 0.40f, 0.20f }; }
    else if (c.contains("piano") || c.contains("keys")){ d = { 1, 0.00f, 0.10f, 0.18f, 0.15f }; }
    else if (c.contains("guitar"))                     { d = { 2, 0.12f, 0.40f, 0.30f, 0.20f }; }
    else if (c.contains("bell") || c.contains("pluck")|| c.contains("crystal"))
                                                       { d = { 2, 0.10f, 0.30f, 0.22f, 0.20f }; }
    else if (c.contains("808") || c.contains("sub") || c.contains("bass"))
                                                       { d = { 1, 0.00f, 0.00f, 0.15f, 0.10f }; }
    else                                               { d = { 2, 0.20f, 0.40f, 0.20f, 0.25f }; }
    return d;
}

static void applyCategoryDsp(juce::AudioProcessor& proc, const juce::String& category, float mainAttackMs)
{
    if (auto* dp = dynamic_cast<DiditagainProcessor*>(&proc))
    {
        const auto d = dspForCategory(category);
        const auto c = category.toLowerCase();
        // Resolve a sensible eqRole baseline per category so V2 presets get
        // the same role-aware HP+LP+trim carving as V1 .diapreset.
        juce::String role2 = "warmth";
        if      (c.contains("piano") || c.contains("keys"))   role2 = "air";
        else if (c.contains("choir") || c.contains("vox"))    role2 = "air";
        else if (c.contains("pad")   || c.contains("texture")) role2 = "air";
        else if (c.contains("guitar"))                        role2 = "texture";
        else if (c.contains("brass") || c.contains("trumpet")) role2 = "warmth";
        else if (c.contains("808")   || c.contains("sub"))    role2 = "sub";
        else if (c.contains("lead"))                          role2 = "lead";

        dp->getSynthEngine().forEachSynthVoice([d, role2, mainAttackMs](SynthVoice& v)
        {
            v.setUnisonRender(d.unisonVoices, d.unisonDetune, d.stereoSpread, d.drift);
            v.setExciterAmount(d.exciter);
            v.setStereoSpreadAmount(d.stereoSpread);
            v.setLayer2EqRole(role2);
            v.setLayer3EqRole(role2 == "air" || role2 == "texture" ? role2 : juce::String("air"));
            v.setLayer4EqRole(role2 == "sub" ? juce::String("sub") : juce::String("sub"));
            v.setLayer2FollowMain(true, mainAttackMs);
            v.setLayer3FollowMain(true, mainAttackMs);
            v.setLayer4FollowMain(true, mainAttackMs);
        });
    }
}




// Map preset category to a reverb character voicing. Categories are matched
// loosely (case-insensitive, substring) so V1/V2/imported names all hit.
static ReverbBlock::Character characterForCategory(const juce::String& categoryIn) noexcept
{
    const auto c = categoryIn.toLowerCase();
    if (c.contains("808") || c.contains("sub"))                              return ReverbBlock::Character::Studio;
    if (c.contains("brass") || c.contains("trumpet") || c.contains("horn") || c.contains("drill") || c.contains("trap")) return ReverbBlock::Character::Trap;
    if (c.contains("guitar"))                                                return ReverbBlock::Character::Vintage;
    if (c.contains("pad") || c.contains("string") || c.contains("texture") || c.contains("ambient")) return ReverbBlock::Character::Dream;
    if (c.contains("choir") || c.contains("vox") || c.contains("vocal"))     return ReverbBlock::Character::Cathedral;
    if (c.contains("bell") || c.contains("pluck") || c.contains("crystal"))  return ReverbBlock::Character::Shimmer;
    if (c.contains("piano") || c.contains("keys"))                           return ReverbBlock::Character::Hall;
    if (c.contains("lead"))                                                  return ReverbBlock::Character::Hall;
    if (c.contains("dark"))                                                  return ReverbBlock::Character::Dark;
    if (c.contains("fx") || c.contains("riser"))                             return ReverbBlock::Character::Cathedral;
    return ReverbBlock::Character::Studio;
}

void applyReverbCharacterForCategory(juce::AudioProcessor& proc, const juce::String& category)
{
    if (auto* dp = dynamic_cast<DiditagainProcessor*>(&proc))
    {
        auto& fx = dp->getSynthEngine().getFx();
        const auto c = category.toLowerCase();
        fx.setReverbCharacter(characterForCategory(category));

        if (c.contains("808") || c.contains("sub"))
        {
            fx.setReverbInputHighPassHz(360.0f); fx.setReverbInputLowPassHz(4200.0f);
            fx.setReverbDiffusion(0.45f); fx.setReverbDucking(0.10f, 4.0f, 180.0f);
            fx.setReverbLowMonoControl(340.0f, 0.0f); fx.setReverbWidth(0.25f);
        }
        else if (c.contains("brass") || c.contains("trumpet") || c.contains("horn") || c.contains("drill") || c.contains("trap"))
        {
            fx.setReverbInputHighPassHz(240.0f); fx.setReverbInputLowPassHz(5600.0f);
            fx.setReverbDiffusion(0.54f); fx.setReverbDucking(0.28f, 4.0f, 220.0f);
            fx.setReverbLowMonoControl(320.0f, 0.03f); fx.setReverbWidth(0.78f);
        }
        else if (c.contains("pad") || c.contains("string") || c.contains("texture") || c.contains("ambient"))
        {
            fx.setReverbInputHighPassHz(340.0f); fx.setReverbInputLowPassHz(7200.0f);
            fx.setReverbDiffusion(0.68f); fx.setReverbDucking(0.27f, 8.0f, 360.0f);
            fx.setReverbLowMonoControl(340.0f, 0.04f); fx.setReverbWidth(0.92f);
        }
        else if (c.contains("choir") || c.contains("vox") || c.contains("vocal"))
        {
            fx.setReverbInputHighPassHz(360.0f); fx.setReverbInputLowPassHz(7600.0f);
            fx.setReverbDiffusion(0.70f); fx.setReverbDucking(0.28f, 9.0f, 390.0f);
            fx.setReverbLowMonoControl(350.0f, 0.04f); fx.setReverbWidth(0.94f);
        }
        else if (c.contains("guitar"))
        {
            fx.setReverbInputHighPassHz(280.0f); fx.setReverbInputLowPassHz(4800.0f);
            fx.setReverbDiffusion(0.58f); fx.setReverbDucking(0.22f, 5.0f, 260.0f);
            fx.setReverbLowMonoControl(300.0f, 0.05f); fx.setReverbWidth(0.72f);
        }
        else if (c.contains("piano") || c.contains("keys"))
        {
            // Pianos/keys: tight cinematic room, no smear, controlled low-mids.
            fx.setReverbInputHighPassHz(260.0f); fx.setReverbInputLowPassHz(6400.0f);
            fx.setReverbDiffusion(0.55f); fx.setReverbDucking(0.20f, 6.0f, 220.0f);
            fx.setReverbLowMonoControl(280.0f, 0.04f); fx.setReverbWidth(0.68f);
        }
        else if (c.contains("lead"))
        {
            fx.setReverbInputHighPassHz(210.0f); fx.setReverbInputLowPassHz(8500.0f);
            fx.setReverbDiffusion(0.62f); fx.setReverbDucking(0.23f, 5.0f, 240.0f);
            fx.setReverbLowMonoControl(300.0f, 0.06f); fx.setReverbWidth(0.86f);
        }
    }
}

static void applyReverbCharacter(juce::AudioProcessor& proc, const juce::String& category)
{
    applyReverbCharacterForCategory(proc, category);
}


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
    // Melodic / sustained categories autoloop by default.
    if (c == "DrillBells" || c == "PainPianos" || c == "ChoirsVox"
        || c == "Guitars"   || c == "DarkPads"  || c == "AlienLeads"
        || c == "Textures")
        return true;
    if (c == "Bass808")  return false; // unless layerLoop / detected loop points
    if (c == "Plucks")   return false; // typically short
    if (c == "FXRisers") return false;
    if (c == "Uncategorized") return true; // melodic-friendly default
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
        // Clamp release so notes actually stop when the key is lifted.
        // Anything longer than ~0.8s sounds like the sample is ignoring
        // note-off and "playing the whole source". Reverb/delay tails
        // still provide ambient space without dragging the dry sample.
        const float clampedRelease = juce::jlimit(0.001f, 0.8f,
                                                  sampleLayer->ampEnv.release);
        setFloat(processor, "env1Release", clampedRelease);
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
        setFloat(processor, "noiseLevel", juce::jlimit(0.0f, 0.18f, airLayer->volume * 0.45f));
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
    setFloat(processor, "fxReverbMix",        p.effects.reverbEnabled ? juce::jlimit(0.0f, 0.38f, p.effects.reverbMix) : 0.0f);
    setFloat(processor, "fxReverbSize",       juce::jlimit(0.0f, 0.72f, p.effects.reverbSize));
    setFloat(processor, "fxDelayMix",         p.effects.delayEnabled  ? p.effects.delayMix  : 0.0f);
    setFloat(processor, "fxDelayFeedback",    p.effects.delayFb);
    setFloat(processor, "fxChorusMix",        p.effects.chorusEnabled ? p.effects.chorusMix : 0.0f);
    setFloat(processor, "fxDistortionAmount", p.effects.satEnabled    ? p.effects.satDrive  : 0.0f);

    // ---- Reverb character voiced per instrument family ----
    applyReverbCharacter(processor, p.category);
    // Per-category hybrid-synth tuning (unison / spread / exciter / drift)
    // plus role-aware EQ carving and followMainEnvelope fade-in.
    const float mainAttackMs = (sampleLayer != nullptr)
        ? juce::jmax(0.0f, sampleLayer->ampEnv.attack * 1000.0f) : 5.0f;
    applyCategoryDsp(processor, p.category, mainAttackMs);


    return out;
}

}} // namespace dida::preset
