#include "PresetTemplateFactory.h"

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

// --- Macro presets (per-category friendly names + simple targets) ---
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

HybridPresetV2 PresetTemplateFactory::build(const juce::String& category,
                                            const juce::String& presetName,
                                            const juce::String& sampleRelPath,
                                            int rootMidi,
                                            const juce::String& rootNote)
{
    HybridPresetV2 p;
    p.schemaVersion = kSchemaVersionV2;
    p.name = presetName;
    p.bank = "User";
    p.category = category;
    p.tags = juce::StringArray{ category.toLowerCase(), "imported", "hybrid" };

    if (category == "DrillBells")
    {
        p.layers.push_back(sampleL(sampleRelPath, rootMidi, rootNote, 0.82f,
                                   e(0.001f, 1.2f, 0.15f, 1.8f), lpf(8500.0f)));
        p.layers.push_back(oscL("layer_2","Sine Body","sine",-12,0,0.22f,true,
                                e(0.001f, 0.85f, 0.05f, 1.1f)));
        p.layers.push_back(noiseL("layer_3", true, 0.04f, e(0.001f, 0.06f, 0.0f, 0.04f), "Air Transient"));
        p.layers.push_back(oscL("layer_4","Shimmer","triangle", 12, 7, 0.07f, true,
                                e(0.02f, 1.5f, 0.12f, 2.0f)));
        p.globalFilter = lpf(9000.0f, 0.15f);
        p.effects.satEnabled = true; p.effects.satDrive = 0.12f; p.effects.satMix = 0.35f;
        p.effects.chorusEnabled = true; p.effects.chorusMix = 0.22f;
        p.effects.delayEnabled  = true; p.effects.delayMix  = 0.12f; p.effects.delayFb = 0.24f;
        p.effects.reverbEnabled = true; p.effects.reverbMix = 0.28f; p.effects.reverbSize = 0.72f;
        p.macros.push_back(mk("macro_1","Darkness", 0.3f,  {{"globalFilter.cutoff", 12000.0f, 1500.0f}}));
        p.macros.push_back(mk("macro_2","Space",    0.5f,  {{"effects.reverb.mix", 0.0f, 0.7f},
                                                            {"effects.delay.mix",  0.0f, 0.4f}}));
        p.macros.push_back(mk("macro_3","Grit",     0.2f,  {{"effects.saturation.drive", 0.0f, 0.6f}}));
        p.macros.push_back(mk("macro_4","Width",    0.5f,  {{"effects.chorus.mix", 0.0f, 0.6f}}));
    }
    else if (category == "Bass808")
    {
        p.layers.push_back(sampleL(sampleRelPath, rootMidi, rootNote, 0.85f,
                                   e(0.001f, 1.0f, 0.85f, 0.8f), lpf(4500.0f, 0.10f, 0.10f)));
        p.layers.push_back(oscL("layer_2","Sine Sub","sine", 0, 0, 0.30f, true,
                                e(0.001f, 1.0f, 0.85f, 0.8f)));
        p.layers.push_back(noiseL("layer_3", false, 0.0f, e(0.001f, 0.05f, 0.0f, 0.02f)));
        p.layers.push_back(oscL("layer_4","Distort Aux","saw", 0, 0, 0.0f, false,
                                e(0.001f, 0.5f, 0.0f, 0.5f)));
        p.globalFilter = lpf(5000.0f, 0.15f);
        p.effects.satEnabled = true; p.effects.satMode = "diode"; p.effects.satDrive = 0.30f; p.effects.satMix = 0.40f;
        p.effects.reverbEnabled = false; p.effects.chorusEnabled = false; p.effects.widthEnabled = false;
        p.macros.push_back(mk("macro_1","Drive", 0.3f, {{"effects.saturation.drive", 0.0f, 0.8f}}));
        p.macros.push_back(mk("macro_2","Glide", 0.0f, {{"glideTime", 0.0f, 0.3f}}));
        p.macros.push_back(mk("macro_3","Tone",  0.5f, {{"globalFilter.cutoff", 600.0f, 8000.0f}}));
        p.macros.push_back(mk("macro_4","Punch", 0.5f, {{"layers[2].volume", 0.0f, 0.6f}}));
    }
    else if (category == "ChoirsVox")
    {
        p.layers.push_back(sampleL(sampleRelPath, rootMidi, rootNote, 0.80f,
                                   e(0.05f, 0.6f, 0.85f, 2.5f), lpf(9000.0f)));
        p.layers.push_back(oscL("layer_2","Shimmer","triangle", 12, 0, 0.08f, true,
                                e(0.08f, 1.0f, 0.7f, 2.5f)));
        p.layers.push_back(noiseL("layer_3", true, 0.03f, e(0.4f, 1.5f, 0.4f, 2.0f), "Air"));
        p.layers.push_back(oscL("layer_4","Body Sub","sine", -12, 0, 0.08f, true,
                                e(0.05f, 1.0f, 0.7f, 2.5f)));
        p.globalFilter = lpf(10000.0f, 0.10f);
        p.effects.reverbEnabled = true; p.effects.reverbMix = 0.45f; p.effects.reverbSize = 0.85f;
        p.effects.chorusEnabled = true; p.effects.chorusMix = 0.20f;
        p.effects.delayEnabled = true;  p.effects.delayMix = 0.10f; p.effects.delayFb = 0.20f;
        p.effects.widthEnabled = true;
        p.macros.push_back(mk("macro_1","Air",      0.5f, {{"layers[3].volume", 0.0f, 0.12f}}));
        p.macros.push_back(mk("macro_2","Space",    0.6f, {{"effects.reverb.mix", 0.0f, 0.8f}}));
        p.macros.push_back(mk("macro_3","Width",    0.5f, {{"effects.chorus.mix", 0.0f, 0.5f}}));
        p.macros.push_back(mk("macro_4","Darkness", 0.3f, {{"globalFilter.cutoff", 12000.0f, 2000.0f}}));
    }
    else if (category == "PainPianos")
    {
        p.layers.push_back(sampleL(sampleRelPath, rootMidi, rootNote, 0.82f,
                                   e(0.003f, 1.2f, 0.55f, 1.6f), lpf(9500.0f)));
        p.layers.push_back(oscL("layer_2","Warm Body","triangle", -12, 0, 0.10f, true,
                                e(0.003f, 1.0f, 0.5f, 1.4f)));
        p.layers.push_back(noiseL("layer_3", true, 0.02f, e(0.001f, 0.05f, 0.0f, 0.04f), "Hammer Air"));
        p.layers.push_back(oscL("layer_4","Pad","sine", 12, 0, 0.0f, false, e(0.4f, 1.0f, 0.6f, 2.0f)));
        p.globalFilter = lpf(10000.0f);
        p.effects.reverbEnabled = true; p.effects.reverbMix = 0.30f; p.effects.reverbSize = 0.65f;
        p.effects.delayEnabled  = true; p.effects.delayMix = 0.08f;
        p.effects.satEnabled    = true; p.effects.satDrive = 0.10f; p.effects.satMix = 0.25f;
        p.macros.push_back(mk("macro_1","Softness", 0.5f, {{"globalFilter.cutoff", 14000.0f, 3000.0f}}));
        p.macros.push_back(mk("macro_2","Room",     0.5f, {{"effects.reverb.mix", 0.0f, 0.7f}}));
        p.macros.push_back(mk("macro_3","Dark",     0.3f, {{"layers[2].volume", 0.0f, 0.4f}}));
        p.macros.push_back(mk("macro_4","Width",    0.5f, {{"effects.chorus.mix", 0.0f, 0.4f}}));
    }
    else if (category == "AlienLeads")
    {
        p.layers.push_back(sampleL(sampleRelPath, rootMidi, rootNote, 0.80f,
                                   e(0.002f, 0.5f, 0.7f, 0.9f), lpf(11000.0f)));
        p.layers.push_back(oscL("layer_2","Square Lead","square", 0, 7, 0.20f, true,
                                e(0.002f, 0.4f, 0.7f, 0.7f)));
        p.layers.push_back(noiseL("layer_3", false, 0.02f, e(0.001f, 0.05f, 0.0f, 0.03f)));
        p.layers.push_back(oscL("layer_4","Sub","sine", -12, 0, 0.10f, true,
                                e(0.002f, 0.4f, 0.7f, 0.7f)));
        p.globalFilter = lpf(11000.0f, 0.18f);
        p.effects.delayEnabled = true; p.effects.delayMix = 0.20f; p.effects.delayFb = 0.30f;
        p.effects.chorusEnabled = true; p.effects.chorusMix = 0.25f;
        p.effects.reverbEnabled = true; p.effects.reverbMix = 0.20f;
        p.macros.push_back(mk("macro_1","Glide", 0.0f, {{"glideTime", 0.0f, 0.4f}}));
        p.macros.push_back(mk("macro_2","Bite",  0.3f, {{"effects.saturation.drive", 0.0f, 0.6f}}));
        p.macros.push_back(mk("macro_3","Space", 0.5f, {{"effects.reverb.mix", 0.0f, 0.6f},
                                                        {"effects.delay.mix",  0.0f, 0.5f}}));
        p.macros.push_back(mk("macro_4","Width", 0.5f, {{"effects.chorus.mix", 0.0f, 0.5f}}));
    }
    else if (category == "Plucks")
    {
        p.layers.push_back(sampleL(sampleRelPath, rootMidi, rootNote, 0.82f,
                                   e(0.001f, 0.4f, 0.0f, 0.6f), lpf(10000.0f)));
        p.layers.push_back(oscL("layer_2","Body","sine", -12, 0, 0.12f, true,
                                e(0.001f, 0.3f, 0.0f, 0.5f)));
        p.layers.push_back(noiseL("layer_3", true, 0.03f, e(0.001f, 0.04f, 0.0f, 0.02f), "Attack Noise"));
        p.layers.push_back(oscL("layer_4","Aux","triangle", 12, 0, 0.0f, false, e(0.01f, 0.5f, 0.0f, 0.7f)));
        p.globalFilter = lpf(10000.0f);
        p.effects.delayEnabled = true; p.effects.delayMix = 0.20f; p.effects.delayFb = 0.30f;
        p.effects.reverbEnabled = true; p.effects.reverbMix = 0.18f; p.effects.reverbSize = 0.5f;
        p.macros.push_back(mk("macro_1","Snap",       0.5f, {{"layers[3].volume", 0.0f, 0.10f}}));
        p.macros.push_back(mk("macro_2","Space",      0.5f, {{"effects.reverb.mix", 0.0f, 0.6f}}));
        p.macros.push_back(mk("macro_3","Brightness", 0.5f, {{"globalFilter.cutoff", 3000.0f, 16000.0f}}));
        p.macros.push_back(mk("macro_4","Width",      0.5f, {{"effects.chorus.mix", 0.0f, 0.4f}}));
    }
    else if (category == "DarkPads" || category == "Textures")
    {
        const bool isTexture = (category == "Textures");
        p.layers.push_back(sampleL(sampleRelPath, rootMidi, rootNote, 0.78f,
                                   e(0.6f, 1.5f, 0.85f, 3.0f), lpf(7000.0f),
                                   /*pitchTracking*/ true, /*oneShot*/ false, /*loop*/ true));
        p.layers.push_back(oscL("layer_2","Body","sine", -12, 0, 0.18f, true,
                                e(0.6f, 1.2f, 0.7f, 2.5f)));
        p.layers.push_back(noiseL("layer_3", true, isTexture ? 0.05f : 0.03f,
                                  e(0.6f, 1.5f, 0.5f, 3.0f), "Air Wash"));
        p.layers.push_back(oscL("layer_4","Shimmer","triangle", 12, 5, 0.10f, true,
                                e(0.8f, 2.0f, 0.6f, 3.5f)));
        p.globalFilter = lpf(8000.0f, 0.18f);
        p.effects.reverbEnabled = true; p.effects.reverbMix = 0.55f; p.effects.reverbSize = 0.90f;
        p.effects.chorusEnabled = true; p.effects.chorusMix = 0.30f;
        p.effects.widthEnabled = true; p.effects.widthAmount = 0.6f;
        p.macros.push_back(mk("macro_1","Motion",   0.5f, {{"effects.chorus.mix", 0.0f, 0.6f}}));
        p.macros.push_back(mk("macro_2","Air",      0.5f, {{"layers[3].volume", 0.0f, 0.12f}}));
        p.macros.push_back(mk("macro_3","Space",    0.6f, {{"effects.reverb.mix", 0.0f, 0.8f}}));
        p.macros.push_back(mk("macro_4","Darkness", 0.4f, {{"globalFilter.cutoff", 12000.0f, 1500.0f}}));
    }
    else if (category == "FXRisers")
    {
        auto L1 = sampleL(sampleRelPath, rootMidi, rootNote, 0.85f,
                          e(0.001f, 4.0f, 0.0f, 3.0f), lpf(14000.0f),
                          /*pitchTracking*/ false, /*oneShot*/ true);
        p.layers.push_back(L1);
        p.layers.push_back(oscL("layer_2","Noise Wash","sine", 0, 0, 0.0f, false, e(0.5f, 2.0f, 0.0f, 2.0f)));
        p.layers.push_back(noiseL("layer_3", false, 0.0f, e(0.5f, 2.0f, 0.0f, 2.0f)));
        p.layers.push_back(oscL("layer_4","Sweep","saw", 0, 0, 0.0f, false, e(0.5f, 2.0f, 0.0f, 2.0f)));
        p.globalFilter = lpf(14000.0f); p.globalFilter.type = "highpass";
        p.effects.reverbEnabled = true; p.effects.reverbMix = 0.5f; p.effects.reverbSize = 0.95f;
        p.effects.widthEnabled = true; p.effects.widthAmount = 0.7f;
        p.macros.push_back(mk("macro_1","Sweep",   0.5f, {{"globalFilter.cutoff", 200.0f, 18000.0f}}));
        p.macros.push_back(mk("macro_2","Space",   0.7f, {{"effects.reverb.mix", 0.0f, 0.9f}}));
        p.macros.push_back(mk("macro_3","Drive",   0.2f, {{"effects.saturation.drive", 0.0f, 0.5f}}));
        p.macros.push_back(mk("macro_4","Width",   0.7f, {{"effects.chorus.mix", 0.0f, 0.5f}}));
    }
    else
    {
        // Generic / Uncategorized fallback: subtle hybrid (sample + sine body).
        p.layers.push_back(sampleL(sampleRelPath, rootMidi, rootNote, 0.82f,
                                   e(0.005f, 1.0f, 0.4f, 1.2f), lpf(10000.0f)));
        p.layers.push_back(oscL("layer_2","Body","sine", -12, 0, 0.12f, true,
                                e(0.005f, 0.8f, 0.3f, 1.0f)));
        p.layers.push_back(noiseL("layer_3", true, 0.02f, e(0.001f, 0.05f, 0.0f, 0.04f)));
        p.layers.push_back(oscL("layer_4","Aux","triangle", 12, 0, 0.0f, false, e(0.02f, 1.0f, 0.1f, 1.5f)));
        p.globalFilter = lpf(10000.0f);
        p.effects.reverbEnabled = true; p.effects.reverbMix = 0.20f;
        p.macros.push_back(mk("macro_1","Brightness", 0.5f, {{"globalFilter.cutoff", 2000.0f, 16000.0f}}));
        p.macros.push_back(mk("macro_2","Space",      0.4f, {{"effects.reverb.mix", 0.0f, 0.6f}}));
        p.macros.push_back(mk("macro_3","Body",       0.4f, {{"layers[2].volume", 0.0f, 0.4f}}));
        p.macros.push_back(mk("macro_4","Width",      0.5f, {{"effects.chorus.mix", 0.0f, 0.4f}}));
    }

    return p;
}

}} // namespace dida::preset
