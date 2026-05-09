#include "PresetTemplateFactory.h"

namespace dida { namespace preset {

static LayerV2 sampleL(const juce::String& src, int midi, const juce::String& note,
                       float vol, AmpEnvV2 env, LayerFilterV2 filt)
{
    LayerV2 L;
    L.id = "layer_1"; L.name = "Main Sample";
    L.type = LayerType::Sample; L.enabled = ! src.isEmpty();
    L.volume = vol; L.source = src;
    L.rootNote = note; L.rootMidi = midi;
    L.pitchTracking = true; L.ampEnv = env;
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

static LayerV2 noiseL(const juce::String& id, bool enabled, float vol, AmpEnvV2 env)
{
    LayerV2 L;
    L.id = id; L.name = "Noise/Air";
    L.type = LayerType::Noise; L.enabled = enabled;
    L.volume = vol; L.ampEnv = env;
    return L;
}

static AmpEnvV2 e(float a, float d, float s, float r) { return {a,d,s,r}; }
static LayerFilterV2 lpf(float cutoff, float res = 0.12f, float drive = 0.05f)
{ LayerFilterV2 f; f.enabled = true; f.type = "lowpass"; f.cutoff = cutoff; f.resonance = res; f.drive = drive; return f; }

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
                                   e(0.001f,1.2f,0.15f,1.8f), lpf(8500.0f)));
        p.layers.push_back(oscL("layer_2","Sine Body","sine",-12,0,0.22f,true,
                                e(0.001f,0.85f,0.05f,1.1f)));
        p.layers.push_back(noiseL("layer_3", false, 0.04f, e(0.001f,0.05f,0.0f,0.02f)));
        p.layers.push_back(oscL("layer_4","Shimmer","triangle",12,7,0.08f,false,
                                e(0.02f,1.5f,0.12f,2.0f)));
        p.globalFilter = lpf(9000.0f, 0.15f);
        p.effects.satEnabled = true; p.effects.satDrive = 0.12f; p.effects.satMix = 0.35f;
        p.effects.chorusEnabled = true; p.effects.chorusMix = 0.22f;
        p.effects.delayEnabled  = true; p.effects.delayMix  = 0.12f; p.effects.delayFb = 0.24f;
        p.effects.reverbEnabled = true; p.effects.reverbMix = 0.28f; p.effects.reverbSize = 0.72f;
    }
    else if (category == "Bass808")
    {
        p.layers.push_back(sampleL(sampleRelPath, 36, "C2", 0.85f,
                                   e(0.001f,1.0f,0.85f,0.8f), lpf(4500.0f, 0.10f, 0.10f)));
        p.layers.push_back(oscL("layer_2","Sine Sub","sine",0,0,0.30f,true,e(0.001f,1.0f,0.85f,0.8f)));
        p.layers.push_back(noiseL("layer_3", false, 0.0f, e(0.001f,0.05f,0.0f,0.02f)));
        p.layers.push_back(oscL("layer_4","Distort","saw",0,0,0.0f,false,e(0.001f,0.5f,0.0f,0.5f)));
        p.globalFilter = lpf(5000.0f, 0.15f);
        p.effects.satEnabled = true; p.effects.satMode = "diode"; p.effects.satDrive = 0.30f; p.effects.satMix = 0.40f;
        p.effects.reverbEnabled = false; p.effects.chorusEnabled = false; p.effects.widthEnabled = false;
    }
    else if (category == "FXRisers")
    {
        auto L1 = sampleL(sampleRelPath, 60, "C4", 0.85f,
                          e(0.001f,4.0f,0.0f,3.0f), lpf(14000.0f));
        L1.pitchTracking = false; L1.oneShotMode = true;
        p.layers.push_back(L1);
        p.layers.push_back(oscL("layer_2","Noise Wash","sine",0,0,0.0f,false,e(0.5f,2.0f,0.0f,2.0f)));
        p.layers.push_back(noiseL("layer_3", false, 0.0f, e(0.5f,2.0f,0.0f,2.0f)));
        p.layers.push_back(oscL("layer_4","Sweep","saw",0,0,0.0f,false,e(0.5f,2.0f,0.0f,2.0f)));
        p.globalFilter = lpf(14000.0f); p.globalFilter.type = "highpass";
        p.effects.reverbEnabled = true; p.effects.reverbMix = 0.5f; p.effects.reverbSize = 0.95f;
        p.effects.widthEnabled = true;  p.effects.widthAmount = 0.7f;
    }
    else
    {
        // Generic / Uncategorized fallback.
        p.layers.push_back(sampleL(sampleRelPath, rootMidi, rootNote, 0.82f,
                                   e(0.005f,1.0f,0.4f,1.2f), lpf(10000.0f)));
        p.layers.push_back(oscL("layer_2","Body","sine",-12,0,0.0f,false,e(0.005f,0.8f,0.1f,1.0f)));
        p.layers.push_back(noiseL("layer_3", false, 0.04f, e(0.001f,0.05f,0.0f,0.02f)));
        p.layers.push_back(oscL("layer_4","Aux","triangle",12,0,0.0f,false,e(0.02f,1.0f,0.1f,1.5f)));
        p.globalFilter = lpf(10000.0f);
        p.effects.reverbEnabled = true; p.effects.reverbMix = 0.18f;
    }

    // Default 4 macros.
    auto macro = [](const char* id, const char* nm, float v) {
        MacroV2 m; m.id = id; m.name = nm; m.value = v; return m;
    };
    p.macros.push_back(macro("macro_1","Darkness", 0.5f));
    p.macros.push_back(macro("macro_2","Space",    0.5f));
    p.macros.push_back(macro("macro_3","Grit",     0.25f));
    p.macros.push_back(macro("macro_4","Width",    0.5f));
    return p;
}

}} // namespace dida::preset
