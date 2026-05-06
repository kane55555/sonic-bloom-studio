#pragma once
#include "ParameterPanel.h"

class MainSynthPanel : public ParameterPanel
{
public:
    explicit MainSynthPanel(juce::AudioProcessorValueTreeState& apvts)
        : ParameterPanel(apvts, "SYNTH ENGINE")
    {
        const juce::StringArray waves { "Sine", "Triangle", "Saw", "Square", "Pulse", "SuperSaw", "FmCarrier", "Wavetable" };
        const juce::StringArray engines { "Subtractive", "FM2", "FM4", "Wavetable", "Layered" };
        const juce::StringArray filters { "LP12", "LP24", "HP12", "HP24", "BP", "Notch" };
        const juce::StringArray noise { "White", "Pink" };

        const int engine = addGroup("ENGINE");
        addChoice(engine, "Mode", "engineMode", engines);
        addKnob(engine, "Poly", "polyphony");
        addToggle(engine, "Mono", "monoMode");
        addKnob(engine, "Glide", "glideTime");
        addKnob(engine, "FM Amt", "fmAmount");
        addKnob(engine, "FM Ratio", "fmRatio");

        const int oscA = addGroup("OSC A");
        addChoice(oscA, "Wave", "oscAWaveform", waves);
        addKnob(oscA, "Level", "oscALevel");
        addKnob(oscA, "Oct", "oscAOctave");
        addKnob(oscA, "Semi", "oscASemi");
        addKnob(oscA, "Detune", "oscADetune");
        addKnob(oscA, "Pulse", "oscAPulseWidth");

        const int oscB = addGroup("OSC B / SUB / NOISE");
        addChoice(oscB, "Wave", "oscBWaveform", waves);
        addKnob(oscB, "B Level", "oscBLevel");
        addKnob(oscB, "B Oct", "oscBOctave");
        addKnob(oscB, "B Semi", "oscBSemi");
        addKnob(oscB, "B Detune", "oscBDetune");
        addToggle(oscB, "Sub", "subOscEnabled");
        addKnob(oscB, "Sub Lvl", "subOscLevel");
        addChoice(oscB, "Noise", "noiseType", noise);
        addKnob(oscB, "Noise Lvl", "noiseLevel");

        const int filter = addGroup("FILTER");
        addChoice(filter, "Type", "filter1Type", filters);
        addKnob(filter, "Cutoff", "filter1Cutoff");
        addKnob(filter, "Reso", "filter1Resonance");
        addKnob(filter, "Drive", "filter1Drive");
        addKnob(filter, "Env", "filter1EnvAmount");
        addKnob(filter, "Key", "filter1KeyTrack");

        const int amp = addGroup("AMP ENV");
        addKnob(amp, "Attack", "env1Attack");
        addKnob(amp, "Decay", "env1Decay");
        addKnob(amp, "Sustain", "env1Sustain");
        addKnob(amp, "Release", "env1Release");

        const int filterEnv = addGroup("FILTER ENV");
        addKnob(filterEnv, "Attack", "env2Attack");
        addKnob(filterEnv, "Decay", "env2Decay");
        addKnob(filterEnv, "Sustain", "env2Sustain");
        addKnob(filterEnv, "Release", "env2Release");
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainSynthPanel)
};