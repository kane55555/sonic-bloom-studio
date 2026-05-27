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
    else if (c.contains("choir") || c.contains("vox")) { d = { 1, 0.00f, 0.00f, 0.00f, 0.00f }; }
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

static bool isNaturalChoirPreset(const HybridPresetV2& p) noexcept
{
    const auto c = p.category.toLowerCase();
    const auto n = p.name.toLowerCase();
    return c.contains("choir") || c.contains("vox") || c.contains("vocal") || n.contains("choir");
}

static float naturalChoirReverbMix(const HybridPresetV2& p) noexcept
{
    const auto n = p.name.toLowerCase();
    if (n.contains("clean playable choir aah")) return 0.10f;
    if (n.contains("dark controlled choir eeh")) return 0.11f;
    if (n.contains("wide heaven choir ooh"))    return 0.15f;
    return (n.contains("wide") || n.contains("heaven")) ? 0.15f : 0.11f;
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
    const bool naturalChoirMode = isNaturalChoirPreset(p);

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
        // ---- Per-category amp-release cap (== FX send envelope) ----
        //
        // Voice signal flow is: sample -> amp envelope -> layer bus ->
        // FX (delay/reverb). So clamping the amp release per category
        // guarantees reverb/delay receive at most `releaseMaxMs` of tail
        // after a MIDI note-off — never the full WAV duration.
        //
        //   Pianos / Rhodes / Keys ........ 250 ms
        //   Guitars (any) ................. 300 ms (450 ms for ambient/wide)
        //   Brass / Sax / Horn / Trumpet .. 180 ms
        //   Choirs / Strings / Pads ....... 450 ms
        //   Leads / Synths ................ 250 ms
        //   808 / Bass / Sub ..............  80 ms
        const auto cLower = p.category.toLowerCase();
        const auto nLower = p.name.toLowerCase();
        float releaseMaxMs = 300.0f;
        if      (cLower.contains("808") || cLower.contains("bass") || cLower.contains("sub")) releaseMaxMs = 80.0f;
        else if (cLower.contains("brass") || cLower.contains("sax") || cLower.contains("horn") || cLower.contains("trumpet")) releaseMaxMs = 180.0f;
        else if (cLower.contains("piano") || cLower.contains("rhodes") || cLower.contains("keys")) releaseMaxMs = 250.0f;
        else if (cLower.contains("lead") || cLower.contains("synth")) releaseMaxMs = 250.0f;
        else if (cLower.contains("choir") || cLower.contains("vox") || cLower.contains("vocal")
              || cLower.contains("string") || cLower.contains("pad") || cLower.contains("texture") || cLower.contains("ambient"))
            releaseMaxMs = 450.0f;
        else if (cLower.contains("guitar"))
            releaseMaxMs = (nLower.contains("ambient") || nLower.contains("wide")) ? 450.0f : 300.0f;
        const float clampedRelease = juce::jlimit(0.001f, releaseMaxMs * 0.001f,
                                                  sampleLayer->ampEnv.release);
        setFloat(processor, "env1Release", clampedRelease);
        setFloat(processor, "oscALevel",   sampleLayer->volume);

        juce::Logger::writeToLog(juce::String("[DIDITAGAIN fx-send] preset=") + p.name
            + " category=" + p.category
            + " fxSendPostEnvelope=true fxSendFollowsAmpEnvelope=true"
            + " noteOffStopsFxSend=true"
            + " ampReleaseMaxMs=" + juce::String(releaseMaxMs, 1)
            + " ampReleaseSec=" + juce::String(clampedRelease, 3)
            + " clearFxOnTransportStop=true transportStopFxFadeMs=120"
            + " clearFxTailOnPresetChange=true");
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
    if (naturalChoirMode)
    {
        setFloat(processor, "oscBLevel", 0.0f);
        setFloat(processor, "oscBOctave", 0.0f);
        setFloat(processor, "oscBSemi", 0.0f);
        setFloat(processor, "oscBDetune", 0.0f);
        setFloat(processor, "oscADetune", 0.0f);
        setFloat(processor, "oscAOctave", 0.0f);
        setFloat(processor, "oscASemi", 0.0f);
        setFloat(processor, "unisonVoices", 1.0f);
        setFloat(processor, "unisonDetune", 0.0f);
        setFloat(processor, "unisonSpread", 0.0f);
        setFloat(processor, "vintageAmount", 0.0f);
    }
    else if (bodyLayer != nullptr && bodyLayer->enabled)
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
    if (naturalChoirMode)
        setFloat(processor, "noiseLevel", 0.0f);
    else if (airLayer != nullptr && airLayer->enabled)
        setFloat(processor, "noiseLevel", juce::jlimit(0.0f, 0.18f, airLayer->volume * 0.45f));
    else
        setFloat(processor, "noiseLevel", 0.0f);

    // ---- Layer 4 shimmer / sub ----
    if (naturalChoirMode)
    {
        setBool (processor, "subOscEnabled", false);
        setFloat(processor, "subOscLevel",   0.0f);
    }
    else if (shimmerLayer != nullptr && shimmerLayer->enabled)
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

    // ---- Effects (scale-safe category caps) ------------------------------
    // The user-facing complaint was that fast notes / scale runs piled the
    // previous notes' reverb + delay tails on top of each other into a muddy
    // pitch cloud. Defence-in-depth here: clamp mix / size / feedback per
    // category, then apply reverb input filtering + ducking + delay ducking.
    struct FxCaps {
        float reverbMixMax  = 0.40f;
        float reverbSizeMax = 0.70f;
        float delayMixMax   = 0.30f;
        float delayFbMax    = 0.40f;
        float reverbHp      = 220.0f;
        float reverbLp      = 7000.0f;
        float reverbDuck    = 0.30f;
        float reverbDuckRel = 220.0f;
        float delayDuck     = 0.45f;
        float delayDuckRel  = 140.0f;
        float chorusMixMax  = 1.0f;
        float satMixMax     = 1.0f;
    };

    // -------- Choir mode detection ----------------------------------------
    // Categories: Choirs, Choirs Ahhh/Ohhh/Ehhh, Choir Aah/Ooh/Eeh, etc.
    const auto cLowerForChoir = p.category.toLowerCase();
    const auto nLowerForChoir = p.name.toLowerCase();
    const bool choirMode = cLowerForChoir.contains("choir")
                        || cLowerForChoir.contains("vox")
                        || cLowerForChoir.contains("vocal")
                        || nLowerForChoir.contains("choir");
    const bool choirWide = choirMode
        && (nLowerForChoir.contains("wide") || nLowerForChoir.contains("heaven"));

    auto fxCapsFor = [choirMode, choirWide](const juce::String& catIn) -> FxCaps
    {
        const auto c = catIn.toLowerCase();
        FxCaps k;
        if (c.contains("rhodes"))                                              { k = { 0.20f, 0.52f, 0.10f, 0.18f, 200.0f, 6500.0f, 0.30f, 160.0f, 0.45f, 140.0f }; }
        else if (c.contains("piano") || c.contains("keys"))                    { k = { 0.18f, 0.48f, 0.08f, 0.18f, 180.0f, 6500.0f, 0.30f, 160.0f, 0.45f, 140.0f }; }
        else if (c.contains("acoustic"))                                       { k = { 0.22f, 0.55f, 0.08f, 0.22f, 220.0f, 5200.0f, 0.35f, 180.0f, 0.45f, 140.0f }; }
        else if (c.contains("guitar"))                                         { k = { 0.28f, 0.58f, 0.10f, 0.22f, 220.0f, 5200.0f, 0.35f, 180.0f, 0.45f, 140.0f }; }
        else if (c.contains("brass") || c.contains("trumpet") || c.contains("horn") || c.contains("sax")) { k = { 0.16f, 0.45f, 0.06f, 0.12f, 250.0f, 4200.0f, 0.45f, 120.0f, 0.50f, 120.0f }; }
        else if (choirMode || c.contains("choir") || c.contains("vox") || c.contains("vocal"))
        {
            // Choir-specific caps: controlled reverb/delay, vowel-friendly band.
            k = { 0.22f, 0.62f, 0.03f, 0.08f, 250.0f, 5500.0f, 0.30f, 280.0f, 0.50f, 160.0f, 0.08f, 0.06f };
        }
        else if (c.contains("string") || c.contains("pad") || c.contains("texture") || c.contains("ambient")) { k = { 0.34f, 0.72f, 0.22f, 0.18f, 200.0f, 7200.0f, 0.25f, 260.0f, 0.45f, 160.0f }; }
        else if (c.contains("bell"))                                           { k = { 0.24f, 0.55f, 0.15f, 0.24f, 350.0f, 8000.0f, 0.30f, 200.0f, 0.45f, 140.0f }; }
        else if (c.contains("pluck"))                                          { k = { 0.22f, 0.50f, 0.14f, 0.22f, 260.0f, 8500.0f, 0.32f, 180.0f, 0.45f, 140.0f }; }
        else if (c.contains("808") || c.contains("bass") || c.contains("sub")) { k = { 0.04f, 0.30f, 0.05f, 0.18f, 500.0f, 4200.0f, 0.20f, 160.0f, 0.40f, 140.0f }; }
        else if (c.contains("lead") || c.contains("synth"))                    { k = { 0.22f, 0.55f, 0.12f, 0.20f, 200.0f, 8500.0f, 0.35f, 150.0f, 0.45f, 140.0f }; }
        else if (c.contains("riser") || c.contains("fx"))                      { k = { 0.40f, 0.85f, 0.30f, 0.30f, 220.0f, 9000.0f, 0.25f, 320.0f, 0.40f, 180.0f }; }
        return k;
    };

    const auto caps = fxCapsFor(p.category);

    auto* fxChainPtr = [&processor]() -> FxChain* {
        if (auto* dp = dynamic_cast<DiditagainProcessor*>(&processor))
            return &dp->getSynthEngine().getFx();
        return nullptr;
    }();
    const bool scaleSafe = fxChainPtr != nullptr && fxChainPtr->getScaleSafeFxMode();
    const float scaleSafeMixMul = scaleSafe ? 0.75f : 1.0f;
    const float scaleSafeFbBias = scaleSafe ? -0.05f : 0.0f;

    auto logClamp = [&](const char* effect, float oldV, float newV, const char* reason) {
        if (std::abs(oldV - newV) > 0.001f)
            juce::Logger::writeToLog(juce::String("[DIDITAGAIN fx-safety] preset=") + p.name
                + " category=" + p.category + " effect=" + effect
                + " old=" + juce::String(oldV, 3) + " new=" + juce::String(newV, 3)
                + " reason=" + reason);
    };

    const float wantReverbMix  = p.effects.reverbEnabled ? p.effects.reverbMix : 0.0f;
    const float wantReverbSize = p.effects.reverbSize;
    const float wantDelayMix   = p.effects.delayEnabled  ? p.effects.delayMix  : 0.0f;
    const float wantDelayFb    = p.effects.delayFb;

    const float reverbMix  = juce::jlimit(0.0f, caps.reverbMixMax,  wantReverbMix  * scaleSafeMixMul);
    const float reverbSize = juce::jlimit(0.0f, caps.reverbSizeMax, wantReverbSize);
    const float delayMix   = juce::jlimit(0.0f, caps.delayMixMax,   wantDelayMix   * scaleSafeMixMul);
    const float delayFb    = juce::jlimit(0.0f, juce::jmax(0.0f, caps.delayFbMax + scaleSafeFbBias), wantDelayFb);

    logClamp("reverbMix",  wantReverbMix,  reverbMix,  "scale-safe reverb cap");
    logClamp("reverbSize", wantReverbSize, reverbSize, "scale-safe reverb cap");
    logClamp("delayMix",   wantDelayMix,   delayMix,   "scale-safe delay cap");
    logClamp("delayFb",    wantDelayFb,    delayFb,    "scale-safe delay feedback cap");

    // ---- Choir-mode chorus/saturation caps -------------------------------
    float wantChorusMix = p.effects.chorusEnabled ? p.effects.chorusMix : 0.0f;
    float wantSatMix    = p.effects.satEnabled    ? p.effects.satDrive  : 0.0f;
    float chorusMixOut  = wantChorusMix;
    float satMixOut     = wantSatMix;
    if (choirMode)
    {
        const float chorusCap = choirWide ? 0.20f : caps.chorusMixMax;
        const float satCap    = caps.satMixMax;
        chorusMixOut = juce::jmin(wantChorusMix, chorusCap);
        satMixOut    = juce::jmin(wantSatMix,    satCap);
        if (std::abs(wantChorusMix - chorusMixOut) > 0.001f)
            juce::Logger::writeToLog(juce::String("[DIDITAGAIN choir-safety] preset=") + p.name
                + " effect=chorus oldValue=" + juce::String(wantChorusMix, 3)
                + " newValue=" + juce::String(chorusMixOut, 3) + " reason=choirMode cap");
        if (std::abs(wantSatMix - satMixOut) > 0.001f)
            juce::Logger::writeToLog(juce::String("[DIDITAGAIN choir-safety] preset=") + p.name
                + " effect=saturation oldValue=" + juce::String(wantSatMix, 3)
                + " newValue=" + juce::String(satMixOut, 3) + " reason=choirMode cap");
        if (std::abs(wantReverbMix - reverbMix) > 0.001f)
            juce::Logger::writeToLog(juce::String("[DIDITAGAIN choir-safety] preset=") + p.name
                + " effect=reverb oldValue=" + juce::String(wantReverbMix, 3)
                + " newValue=" + juce::String(reverbMix, 3) + " reason=choirMode cap");
        if (std::abs(wantDelayMix - delayMix) > 0.001f)
            juce::Logger::writeToLog(juce::String("[DIDITAGAIN choir-safety] preset=") + p.name
                + " effect=delay oldValue=" + juce::String(wantDelayMix, 3)
                + " newValue=" + juce::String(delayMix, 3) + " reason=choirMode cap");
    }

    setFloat(processor, "fxReverbMix",        reverbMix);
    setFloat(processor, "fxReverbSize",       reverbSize);
    setFloat(processor, "fxDelayMix",         delayMix);
    setFloat(processor, "fxDelayFeedback",    delayFb);
    setFloat(processor, "fxChorusMix",        chorusMixOut);
    setFloat(processor, "fxDistortionAmount", satMixOut);

    // ---- Reverb character voiced per instrument family ----
    applyReverbCharacter(processor, p.category);

    // ---- Reverb input filtering + delay ducking per category ----
    if (fxChainPtr != nullptr)
    {
        fxChainPtr->setReverbInputHighPassHz(caps.reverbHp);
        fxChainPtr->setReverbInputLowPassHz(caps.reverbLp);
        fxChainPtr->setReverbDucking(caps.reverbDuck, 8.0f, caps.reverbDuckRel);
        fxChainPtr->setDelayDucking(caps.delayDuck, 5.0f, caps.delayDuckRel);
        fxChainPtr->setNoteDensityFxReductionEnabled(true);
        if (choirMode)
        {
            // Tighten low-mid mono control on choir wet return.
            fxChainPtr->setReverbLowMonoControl(250.0f, 0.0f);
        }
        // Drain old reverb/delay tails so the previous preset doesn't bleed in.
        if (fxChainPtr->getClearFxTailOnPresetChange())
            fxChainPtr->clearTimeFxTails();
    }

    // ---- Choir amp-release + FX-send-release behavior --------------------
    if (choirMode && sampleLayer != nullptr)
    {
        if (auto* dp = dynamic_cast<DiditagainProcessor*>(&processor))
        {
            const float requestedReleaseMs = sampleLayer->ampEnv.release * 1000.0f;
            const float ampReleaseMs = juce::jlimit(300.0f, 900.0f, requestedReleaseMs);
            if (std::abs(requestedReleaseMs - ampReleaseMs) > 0.5f)
                juce::Logger::writeToLog(juce::String("[DIDITAGAIN choir-safety] amp release clamped preset=")
                    + p.name + " old=" + juce::String(requestedReleaseMs, 1)
                    + " new=" + juce::String(ampReleaseMs, 1));
            setFloat(processor, "env1Release", ampReleaseMs * 0.001f);

            // FX send must be shorter than amp release: min(180, ampRel * 0.35),
            // floored at 60ms so it doesn't choke the immediate attack tail.
            const float fxSendReleaseMs = juce::jlimit(60.0f, 180.0f,
                                                       juce::jmin(180.0f, ampReleaseMs * 0.35f));
            dp->getSynthEngine().setFxSendReleaseMsForAll(fxSendReleaseMs);

            juce::Logger::writeToLog(juce::String("[DIDITAGAIN choir-fx-send] preset=") + p.name
                + " ampReleaseMs=" + juce::String(ampReleaseMs, 1)
                + " fxSendReleaseMs=" + juce::String(fxSendReleaseMs, 1)
                + " fxSendChokedOnNoteOff=true");

            // Polyphony hint: cap default at 8 for choir presets, allow up to
            // 10 for wide/heaven variants. We always clamp DOWN — the user can
            // re-raise polyphony post-load if they explicitly want more voices.
            float currentPoly = 0.0f;
            if (auto* pp = findParam(processor, "polyphony"))
                if (auto* rr = dynamic_cast<juce::RangedAudioParameter*>(pp))
                    currentPoly = rr->convertFrom0to1(rr->getValue());
            const int choirPolyTarget = choirWide ? 10 : 8;
            if ((int) std::lround(currentPoly) > choirPolyTarget)
                setFloat(processor, "polyphony", (float) choirPolyTarget);
        }
    }

    // Per-category hybrid-synth tuning (unison / spread / exciter / drift)
    // plus role-aware EQ carving and followMainEnvelope fade-in.
    const float mainAttackMs = (sampleLayer != nullptr)
        ? juce::jmax(0.0f, sampleLayer->ampEnv.attack * 1000.0f) : 5.0f;
    applyCategoryDsp(processor, p.category, mainAttackMs);


    return out;
}

}} // namespace dida::preset
