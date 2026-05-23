#include "PresetTemplateFactory.h"
#include <tuple>
#include <initializer_list>

namespace dida { namespace preset {

static LayerV2 sampleL(const juce::String& src, int midi, const juce::String& note,
                       float vol, AmpEnvV2 env, LayerFilterV2 filt,
                       bool pitchTracking = true, bool oneShot = false, bool loop = false)
{
    LayerV2 L;
    L.id = "layer_1"; L.name = "Main Sample";
    L.type = LayerType::Sample; L.enabled = ! src.isEmpty();
    L.volume = vol; L.source = src;
    L.rootNote = note; L.rootMidi = midi;
    L.pitchTracking = pitchTracking;
    L.oneShotMode = oneShot;
    L.loop = loop;
    L.ampEnv = env;
    L.hasFilter = true; L.filter = filt;
    return L;
}

static LayerV2 oscL(const juce::String& id, const juce::String& name,
                    const juce::String& wave, int pitch, int fine, float vol, bool enabled,
                    AmpEnvV2 env)
{
    LayerV2 L;
    L.id = id; L.name = name;
    L.type = LayerType::Oscillator; L.enabled = enabled;
    L.waveform = wave; L.pitchSemis = pitch; L.fineCents = fine;
    L.volume = vol; L.ampEnv = env;
    return L;
}

static LayerV2 noiseL(const juce::String& id, bool enabled, float vol, AmpEnvV2 env,
                      const juce::String& nm = "Noise/Air")
{
    LayerV2 L;
    L.id = id; L.name = nm;
    L.type = LayerType::Noise; L.enabled = enabled;
    L.volume = vol; L.ampEnv = env;
    return L;
}

static AmpEnvV2 e(float a, float d, float s, float r) { return {a,d,s,r}; }
static LayerFilterV2 lpf(float cutoff, float res = 0.12f, float drive = 0.05f)
{ LayerFilterV2 f; f.enabled = true; f.type = "lowpass"; f.cutoff = cutoff; f.resonance = res; f.drive = drive; return f; }

static MacroV2 mk(const char* id, const char* nm, float v,
                  std::initializer_list<std::tuple<const char*, float, float>> targets)
{
    MacroV2 m; m.id = id; m.name = nm; m.value = v;
    for (const auto& t : targets) {
        MacroTargetV2 tt; tt.path = std::get<0>(t); tt.min = std::get<1>(t); tt.max = std::get<2>(t);
        m.targets.push_back(tt);
    }
    return m;
}

// -----------------------------------------------------------------------------
//  Per-category configurators — retuned for premium / Zenology-Pro vibe.
//  The actual reverb voicing (pre-delay, diffusion, modulation, damping,
//  saturation) is picked by category in HybridPresetApplier via the new
//  ReverbBlock::Character system. These configurators only set the *amounts*.
// -----------------------------------------------------------------------------

static void configureDrillBells(HybridPresetV2& p, const juce::String& src, int midi, const juce::String& note)
{
    // Bells: bright, emotional, dreamy, lush tails, soft modulation.
    p.layers.push_back(sampleL(src, midi, note, 0.84f, e(0.001f, 1.3f, 0.20f, 2.4f), lpf(9500.0f, 0.10f)));
    p.layers.push_back(oscL("layer_2","Sine Body","sine",-12,0,0.20f,true, e(0.001f, 0.9f, 0.05f, 1.4f)));
    p.layers.push_back(noiseL("layer_3", true, 0.03f, e(0.001f, 0.05f, 0.0f, 0.04f), "Air Transient"));
    p.layers.push_back(oscL("layer_4","Shimmer","triangle", 12, 7, 0.10f, true, e(0.02f, 1.7f, 0.18f, 2.6f)));
    p.globalFilter = lpf(10500.0f, 0.12f);
    p.effects.satEnabled = true; p.effects.satDrive = 0.10f; p.effects.satMix = 0.30f;
    p.effects.chorusEnabled = true; p.effects.chorusMix = 0.28f;
    p.effects.delayEnabled  = true; p.effects.delayMix  = 0.14f; p.effects.delayFb = 0.26f;
    p.effects.reverbEnabled = true; p.effects.reverbMix = 0.30f; p.effects.reverbSize = 0.70f;
    p.effects.widthEnabled  = true; p.effects.widthAmount = 0.65f;
    p.macros.push_back(mk("macro_1","Darkness", 0.3f, {{"globalFilter.cutoff", 13000.0f, 1800.0f}}));
    p.macros.push_back(mk("macro_2","Space",    0.55f,{{"effects.reverb.mix", 0.0f, 0.62f}, {"effects.delay.mix", 0.0f, 0.4f}}));
    p.macros.push_back(mk("macro_3","Grit",     0.2f, {{"effects.saturation.drive", 0.0f, 0.55f}}));
    p.macros.push_back(mk("macro_4","Width",    0.6f, {{"effects.chorus.mix", 0.0f, 0.65f}}));
}

static void configureBass808(HybridPresetV2& p, const juce::String& src, int midi, const juce::String& note)
{
    // 808: nearly dry, mono low end, very tight ambience, no excessive mod.
    p.layers.push_back(sampleL(src, midi, note, 0.88f, e(0.001f, 1.0f, 0.85f, 0.7f), lpf(4500.0f, 0.10f, 0.10f)));
    p.layers.push_back(oscL("layer_2","Sine Sub","sine", 0, 0, 0.32f, true, e(0.001f, 1.0f, 0.85f, 0.7f)));
    p.layers.push_back(noiseL("layer_3", false, 0.0f, e(0.001f, 0.05f, 0.0f, 0.02f)));
    p.layers.push_back(oscL("layer_4","Distort Aux","saw", 0, 0, 0.0f, false, e(0.001f, 0.5f, 0.0f, 0.5f)));
    p.globalFilter = lpf(5000.0f, 0.15f);
    p.effects.satEnabled = true; p.effects.satMode = "diode"; p.effects.satDrive = 0.30f; p.effects.satMix = 0.40f;
    p.effects.reverbEnabled = true; p.effects.reverbMix = 0.04f; p.effects.reverbSize = 0.35f;
    p.effects.chorusEnabled = false; p.effects.widthEnabled = false;
    p.effects.delayEnabled = false;
    p.macros.push_back(mk("macro_1","Drive", 0.3f, {{"effects.saturation.drive", 0.0f, 0.8f}}));
    p.macros.push_back(mk("macro_2","Glide", 0.0f, {{"glideTime", 0.0f, 0.3f}}));
    p.macros.push_back(mk("macro_3","Tone",  0.5f, {{"globalFilter.cutoff", 600.0f, 8000.0f}}));
    p.macros.push_back(mk("macro_4","Punch", 0.5f, {{"layers[2].volume", 0.0f, 0.6f}}));
}

static void configureChoirsVox(HybridPresetV2& p, const juce::String& src, int midi, const juce::String& note)
{
    // Choirs / pads: huge cinematic image, long release, deep modulation.
    p.layers.push_back(sampleL(src, midi, note, 0.80f, e(0.18f, 0.8f, 0.85f, 3.2f), lpf(8500.0f, 0.10f)));
    p.layers.push_back(oscL("layer_2","Shimmer","triangle", 12, 0, 0.10f, true, e(0.20f, 1.2f, 0.75f, 3.0f)));
    p.layers.push_back(noiseL("layer_3", true, 0.04f, e(0.4f, 1.5f, 0.45f, 2.5f), "Air"));
    p.layers.push_back(oscL("layer_4","Body Sub","sine", -12, 0, 0.10f, true, e(0.12f, 1.0f, 0.7f, 3.0f)));
    p.globalFilter = lpf(9500.0f, 0.10f);
    p.effects.reverbEnabled = true; p.effects.reverbMix = 0.48f; p.effects.reverbSize = 0.84f;
    p.effects.chorusEnabled = true; p.effects.chorusMix = 0.30f;
    p.effects.delayEnabled = true;  p.effects.delayMix = 0.16f; p.effects.delayFb = 0.28f;
    p.effects.widthEnabled = true;  p.effects.widthAmount = 0.8f;
    p.macros.push_back(mk("macro_1","Air",      0.5f, {{"layers[3].volume", 0.0f, 0.15f}}));
    p.macros.push_back(mk("macro_2","Space",    0.7f, {{"effects.reverb.mix", 0.0f, 0.68f}}));
    p.macros.push_back(mk("macro_3","Width",    0.6f, {{"effects.chorus.mix", 0.0f, 0.55f}}));
    p.macros.push_back(mk("macro_4","Darkness", 0.3f, {{"globalFilter.cutoff", 12000.0f, 2000.0f}}));
}

static void configurePainPianos(HybridPresetV2& p, const juce::String& src, int midi, const juce::String& note)
{
    // Pianos / bells: emotional, lush tails, bright but smooth.
    p.layers.push_back(sampleL(src, midi, note, 0.84f, e(0.003f, 1.2f, 0.55f, 2.0f), lpf(9500.0f, 0.10f)));
    p.layers.push_back(oscL("layer_2","Warm Body","triangle", -12, 0, 0.12f, true, e(0.003f, 1.0f, 0.5f, 1.6f)));
    p.layers.push_back(noiseL("layer_3", true, 0.02f, e(0.001f, 0.05f, 0.0f, 0.04f), "Hammer Air"));
    p.layers.push_back(oscL("layer_4","Pad","sine", 12, 0, 0.05f, true, e(0.4f, 1.0f, 0.5f, 2.4f)));
    p.globalFilter = lpf(10500.0f, 0.10f);
    p.effects.reverbEnabled = true; p.effects.reverbMix = 0.34f; p.effects.reverbSize = 0.72f;
    p.effects.delayEnabled  = true; p.effects.delayMix  = 0.12f; p.effects.delayFb = 0.24f;
    p.effects.chorusEnabled = true; p.effects.chorusMix = 0.18f;
    p.effects.satEnabled    = true; p.effects.satDrive  = 0.08f; p.effects.satMix = 0.22f;
    p.effects.widthEnabled  = true; p.effects.widthAmount = 0.55f;
    p.macros.push_back(mk("macro_1","Softness", 0.5f, {{"globalFilter.cutoff", 14000.0f, 3000.0f}}));
    p.macros.push_back(mk("macro_2","Room",     0.5f, {{"effects.reverb.mix", 0.0f, 0.7f}}));
    p.macros.push_back(mk("macro_3","Dark",     0.3f, {{"layers[2].volume", 0.0f, 0.4f}}));
    p.macros.push_back(mk("macro_4","Width",    0.5f, {{"effects.chorus.mix", 0.0f, 0.45f}}));
}

static void configureAlienLeads(HybridPresetV2& p, const juce::String& src, int midi, const juce::String& note)
{
    // Synth leads: supersaw-style width, lush, emotional, wide stereo.
    p.layers.push_back(sampleL(src, midi, note, 0.78f, e(0.002f, 0.5f, 0.7f, 0.9f), lpf(11500.0f, 0.10f)));
    p.layers.push_back(oscL("layer_2","Detune Saw","saw", 0, 9, 0.26f, true, e(0.002f, 0.5f, 0.7f, 0.9f)));
    p.layers.push_back(noiseL("layer_3", false, 0.02f, e(0.001f, 0.05f, 0.0f, 0.03f)));
    p.layers.push_back(oscL("layer_4","Sub","sine", -12, 0, 0.12f, true, e(0.002f, 0.4f, 0.7f, 0.8f)));
    p.globalFilter = lpf(11500.0f, 0.15f);
    p.effects.delayEnabled  = true; p.effects.delayMix  = 0.22f; p.effects.delayFb = 0.32f;
    p.effects.chorusEnabled = true; p.effects.chorusMix = 0.36f;
    p.effects.reverbEnabled = true; p.effects.reverbMix = 0.28f; p.effects.reverbSize = 0.70f;
    p.effects.satEnabled    = true; p.effects.satDrive  = 0.18f; p.effects.satMix = 0.35f;
    p.effects.widthEnabled  = true; p.effects.widthAmount = 0.7f;
    p.macros.push_back(mk("macro_1","Glide", 0.0f, {{"glideTime", 0.0f, 0.4f}}));
    p.macros.push_back(mk("macro_2","Bite",  0.3f, {{"effects.saturation.drive", 0.0f, 0.7f}}));
    p.macros.push_back(mk("macro_3","Space", 0.55f, {{"effects.reverb.mix", 0.0f, 0.6f}, {"effects.delay.mix", 0.0f, 0.5f}}));
    p.macros.push_back(mk("macro_4","Width", 0.6f, {{"effects.chorus.mix", 0.0f, 0.6f}}));
}

static void configurePlucks(HybridPresetV2& p, const juce::String& src, int midi, const juce::String& note)
{
    // Plucks (treated like bell-family): shimmer character via category.
    p.layers.push_back(sampleL(src, midi, note, 0.84f, e(0.001f, 0.45f, 0.0f, 0.9f), lpf(11000.0f, 0.10f)));
    p.layers.push_back(oscL("layer_2","Body","sine", -12, 0, 0.12f, true, e(0.001f, 0.3f, 0.0f, 0.6f)));
    p.layers.push_back(noiseL("layer_3", true, 0.03f, e(0.001f, 0.04f, 0.0f, 0.02f), "Attack Noise"));
    p.layers.push_back(oscL("layer_4","Aux","triangle", 12, 0, 0.05f, true, e(0.01f, 0.7f, 0.05f, 1.0f)));
    p.globalFilter = lpf(11000.0f, 0.10f);
    p.effects.delayEnabled  = true; p.effects.delayMix  = 0.22f; p.effects.delayFb = 0.30f;
    p.effects.chorusEnabled = true; p.effects.chorusMix = 0.20f;
    p.effects.reverbEnabled = true; p.effects.reverbMix = 0.26f; p.effects.reverbSize = 0.62f;
    p.effects.widthEnabled  = true; p.effects.widthAmount = 0.65f;
    p.macros.push_back(mk("macro_1","Snap",       0.5f, {{"layers[3].volume", 0.0f, 0.10f}}));
    p.macros.push_back(mk("macro_2","Space",      0.5f, {{"effects.reverb.mix", 0.0f, 0.65f}}));
    p.macros.push_back(mk("macro_3","Brightness", 0.5f, {{"globalFilter.cutoff", 3000.0f, 16000.0f}}));
    p.macros.push_back(mk("macro_4","Width",      0.5f, {{"effects.chorus.mix", 0.0f, 0.45f}}));
}

static void configureDarkPadsOrTextures(HybridPresetV2& p, const juce::String& src, int midi, const juce::String& note, bool isTexture)
{
    // Pads: ambient, huge stereo image, darker damping, deep modulation.
    p.layers.push_back(sampleL(src, midi, note, 0.76f, e(0.40f, 1.8f, 0.85f, 3.5f), lpf(5500.0f, 0.18f), true, false, true));
    p.layers.push_back(oscL("layer_2","Body","sine", -12, 0, 0.18f, true, e(0.50f, 1.4f, 0.75f, 3.0f)));
    p.layers.push_back(noiseL("layer_3", true, isTexture ? 0.06f : 0.035f, e(0.6f, 1.6f, 0.55f, 3.2f), "Air Wash"));
    p.layers.push_back(oscL("layer_4","Shimmer","triangle", 12, 5, 0.12f, true, e(0.8f, 2.2f, 0.65f, 3.8f)));
    p.globalFilter = lpf(6500.0f, 0.18f);
    p.effects.reverbEnabled = true; p.effects.reverbMix = 0.58f; p.effects.reverbSize = 0.95f;
    p.effects.chorusEnabled = true; p.effects.chorusMix = 0.38f;
    p.effects.delayEnabled  = true; p.effects.delayMix  = 0.15f; p.effects.delayFb = 0.28f;
    p.effects.widthEnabled  = true; p.effects.widthAmount = 0.85f;
    p.macros.push_back(mk("macro_1","Motion",   0.55f,{{"effects.chorus.mix", 0.0f, 0.65f}}));
    p.macros.push_back(mk("macro_2","Air",      0.5f, {{"layers[3].volume", 0.0f, 0.18f}}));
    p.macros.push_back(mk("macro_3","Space",    0.7f, {{"effects.reverb.mix", 0.0f, 0.9f}}));
    p.macros.push_back(mk("macro_4","Darkness", 0.45f,{{"globalFilter.cutoff", 12000.0f, 1200.0f}}));
}

static void configureFXRisers(HybridPresetV2& p, const juce::String& src, int midi, const juce::String& note)
{
    p.layers.push_back(sampleL(src, midi, note, 0.85f, e(0.001f, 4.0f, 0.0f, 3.0f), lpf(14000.0f), false, true));
    p.layers.push_back(oscL("layer_2","Noise Wash","sine", 0, 0, 0.0f, false, e(0.5f, 2.0f, 0.0f, 2.0f)));
    p.layers.push_back(noiseL("layer_3", false, 0.0f, e(0.5f, 2.0f, 0.0f, 2.0f)));
    p.layers.push_back(oscL("layer_4","Sweep","saw", 0, 0, 0.0f, false, e(0.5f, 2.0f, 0.0f, 2.0f)));
    p.globalFilter = lpf(14000.0f); p.globalFilter.type = "highpass";
    p.effects.reverbEnabled = true; p.effects.reverbMix = 0.55f; p.effects.reverbSize = 0.97f;
    p.effects.widthEnabled  = true; p.effects.widthAmount = 0.85f;
    p.effects.chorusEnabled = true; p.effects.chorusMix = 0.30f;
    p.macros.push_back(mk("macro_1","Sweep", 0.5f, {{"globalFilter.cutoff", 200.0f, 18000.0f}}));
    p.macros.push_back(mk("macro_2","Space", 0.7f, {{"effects.reverb.mix", 0.0f, 0.9f}}));
    p.macros.push_back(mk("macro_3","Drive", 0.2f, {{"effects.saturation.drive", 0.0f, 0.5f}}));
    p.macros.push_back(mk("macro_4","Width", 0.7f, {{"effects.chorus.mix", 0.0f, 0.55f}}));
}

static void configureUncategorized(HybridPresetV2& p, const juce::String& src, int midi, const juce::String& note)
{
    p.layers.push_back(sampleL(src, midi, note, 0.82f, e(0.005f, 1.0f, 0.4f, 1.4f), lpf(10000.0f)));
    p.layers.push_back(oscL("layer_2","Body","sine", -12, 0, 0.12f, true, e(0.005f, 0.8f, 0.3f, 1.0f)));
    p.layers.push_back(noiseL("layer_3", true, 0.02f, e(0.001f, 0.05f, 0.0f, 0.04f)));
    p.layers.push_back(oscL("layer_4","Aux","triangle", 12, 0, 0.0f, false, e(0.02f, 1.0f, 0.1f, 1.5f)));
    p.globalFilter = lpf(10000.0f);
    p.effects.reverbEnabled = true; p.effects.reverbMix = 0.26f; p.effects.reverbSize = 0.65f;
    p.effects.chorusEnabled = true; p.effects.chorusMix = 0.18f;
    p.effects.widthEnabled  = true; p.effects.widthAmount = 0.5f;
    p.macros.push_back(mk("macro_1","Brightness", 0.5f, {{"globalFilter.cutoff", 2000.0f, 16000.0f}}));
    p.macros.push_back(mk("macro_2","Space",      0.4f, {{"effects.reverb.mix", 0.0f, 0.6f}}));
    p.macros.push_back(mk("macro_3","Body",       0.4f, {{"layers[2].volume", 0.0f, 0.4f}}));
    p.macros.push_back(mk("macro_4","Width",      0.5f, {{"effects.chorus.mix", 0.0f, 0.45f}}));
}

// ---- New per-category configurators for premium families ----

static void configureTrapBrass(HybridPresetV2& p, const juce::String& src, int midi, const juce::String& note)
{
    // Zenology "Biggie Brass" / Nexus trap brass: cutoff 4-6k, octave layer,
    // detune 5-12c, short attack, wide, strong upper mids, fast transient.
    p.layers.push_back(sampleL(src, midi, note, 0.86f, e(0.002f, 0.55f, 0.78f, 0.45f), lpf(5000.0f, 0.18f, 0.12f)));
    p.layers.push_back(oscL("layer_2","Octave Saw","saw", 12, 8, 0.28f, true, e(0.002f, 0.55f, 0.78f, 0.45f)));
    p.layers.push_back(noiseL("layer_3", true, 0.04f, e(0.001f, 0.05f, 0.0f, 0.04f), "Air Snap"));
    p.layers.push_back(oscL("layer_4","Sub","sine", -12, 0, 0.10f, true, e(0.002f, 0.6f, 0.6f, 0.6f)));
    p.globalFilter = lpf(5500.0f, 0.20f, 0.10f);
    p.effects.satEnabled    = true; p.effects.satDrive  = 0.55f; p.effects.satMix = 0.55f;
    p.effects.chorusEnabled = true; p.effects.chorusMix = 0.18f;
    p.effects.delayEnabled  = true; p.effects.delayMix  = 0.10f; p.effects.delayFb = 0.20f;
    p.effects.reverbEnabled = true; p.effects.reverbMix = 0.14f; p.effects.reverbSize = 0.55f;
    p.effects.widthEnabled  = true; p.effects.widthAmount = 0.7f;
    p.macros.push_back(mk("macro_1","Tone",   0.6f, {{"globalFilter.cutoff", 2500.0f, 7500.0f}}));
    p.macros.push_back(mk("macro_2","Drive",  0.55f,{{"effects.saturation.drive", 0.0f, 0.85f}}));
    p.macros.push_back(mk("macro_3","Space",  0.4f, {{"effects.reverb.mix", 0.0f, 0.35f}}));
    p.macros.push_back(mk("macro_4","Width",  0.7f, {{"effects.chorus.mix", 0.0f, 0.5f}}));
}

static void configureGuitars(HybridPresetV2& p, const juce::String& src, int midi, const juce::String& note)
{
    // Emotional/analog/ambient: RC-20/Lexicon vibe — filtered highs, subtle
    // saturation, wide stereo chorus, darker reverb, softer delay feedback.
    p.layers.push_back(sampleL(src, midi, note, 0.82f, e(0.003f, 0.8f, 0.65f, 1.4f), lpf(3500.0f, 0.10f)));
    p.layers.push_back(oscL("layer_2","Body","sine", -12, 0, 0.06f, true, e(0.005f, 0.7f, 0.55f, 1.2f)));
    p.layers.push_back(noiseL("layer_3", true, 0.025f, e(0.001f, 0.04f, 0.0f, 0.03f), "Pick Air"));
    p.layers.push_back(oscL("layer_4","Aux","triangle", 12, 4, 0.04f, true, e(0.02f, 0.9f, 0.2f, 1.6f)));
    p.globalFilter = lpf(4000.0f, 0.12f);
    p.effects.satEnabled    = true; p.effects.satDrive  = 0.20f; p.effects.satMix = 0.40f;
    p.effects.chorusEnabled = true; p.effects.chorusMix = 0.40f;
    p.effects.delayEnabled  = true; p.effects.delayMix  = 0.20f; p.effects.delayFb = 0.22f;
    p.effects.reverbEnabled = true; p.effects.reverbMix = 0.30f; p.effects.reverbSize = 0.65f;
    p.effects.widthEnabled  = true; p.effects.widthAmount = 0.7f;
    p.macros.push_back(mk("macro_1","Tone",     0.45f,{{"globalFilter.cutoff", 1500.0f, 6500.0f}}));
    p.macros.push_back(mk("macro_2","Drift",    0.5f, {{"effects.chorus.mix", 0.0f, 0.6f}}));
    p.macros.push_back(mk("macro_3","Space",    0.55f,{{"effects.reverb.mix", 0.0f, 0.7f}, {"effects.delay.mix", 0.0f, 0.45f}}));
    p.macros.push_back(mk("macro_4","Warmth",   0.4f, {{"effects.saturation.drive", 0.0f, 0.55f}}));
}

static void makeBasePreset(HybridPresetV2& p, const juce::String& presetName, const juce::String& category)
{
    p.schemaVersion = kSchemaVersionV2;
    p.name = presetName;
    p.bank = "User";
    p.category = category;
    p.tags = juce::StringArray{ category.toLowerCase(), "imported", "hybrid" };
}

HybridPresetV2 PresetTemplateFactory::build(const juce::String& category,
                                            const juce::String& presetName,
                                            const juce::String& sampleRelPath,
                                            int rootMidi,
                                            const juce::String& rootNote)
{
    HybridPresetV2 p;
    makeBasePreset(p, presetName, category);

    const auto lc = category.toLowerCase();

    if      (category == "DrillBells")                                  configureDrillBells   (p, sampleRelPath, rootMidi, rootNote);
    else if (category == "Bass808" || lc.contains("808"))               configureBass808      (p, sampleRelPath, rootMidi, rootNote);
    else if (category == "ChoirsVox" || lc.contains("choir") || lc.contains("vox") || lc.contains("vocal"))
                                                                        configureChoirsVox    (p, sampleRelPath, rootMidi, rootNote);
    else if (category == "PainPianos" || lc.contains("piano") || lc.contains("keys"))
                                                                        configurePainPianos   (p, sampleRelPath, rootMidi, rootNote);
    else if (category == "AlienLeads" || lc.contains("lead"))           configureAlienLeads   (p, sampleRelPath, rootMidi, rootNote);
    else if (category == "Plucks" || lc.contains("pluck") || lc.contains("bell"))
                                                                        configurePlucks       (p, sampleRelPath, rootMidi, rootNote);
    else if (category == "DarkPads")                                    configureDarkPadsOrTextures(p, sampleRelPath, rootMidi, rootNote, false);
    else if (category == "Textures" || lc.contains("pad") || lc.contains("texture") || lc.contains("ambient"))
                                                                        configureDarkPadsOrTextures(p, sampleRelPath, rootMidi, rootNote, true);
    else if (category == "FXRisers" || lc.contains("riser") || lc == "fx")
                                                                        configureFXRisers     (p, sampleRelPath, rootMidi, rootNote);
    else if (lc.contains("brass") || lc.contains("drill") || lc.contains("trap"))
                                                                        configureTrapBrass    (p, sampleRelPath, rootMidi, rootNote);
    else if (lc.contains("guitar"))                                     configureGuitars      (p, sampleRelPath, rootMidi, rootNote);
    else                                                                configureUncategorized(p, sampleRelPath, rootMidi, rootNote);

    return p;
}

}} // namespace dida::preset
