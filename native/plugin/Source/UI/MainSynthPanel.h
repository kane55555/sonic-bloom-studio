#pragma once
//==============================================================================
//  MainSynthPanel.h — The main "synth" tab: oscillators, filter, envelopes,
//  LFOs, FX, master meter, and 8 macro knobs. Pure UI; all parameter binding
//  is via APVTS attachments so the host controls truth.
//==============================================================================
#include <JuceHeader.h>
#include "Theme.h"
#include "KnobLookAndFeel.h"

class MainSynthPanel : public juce::Component
{
public:
    explicit MainSynthPanel(juce::AudioProcessorValueTreeState& apvtsRef)
        : apvts(apvtsRef)
    {
        setLookAndFeel(&knobLAF);

        // Build controls in declarative groups.
        addOsc("OSC A", "oscALevel",  "oscADetune",  "oscAPulseWidth", oscAGroup);
        addOsc("OSC B", "oscBLevel",  "oscBDetune",  "oscAPulseWidth", oscBGroup);
        addFilter();
        addEnv("AMP",    "env1");
        addEnv("FILTER", "env2");
        addEnv("MOD",    "env3");
        addFx();
        addMacros();
    }

    ~MainSynthPanel() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        const auto& C = Theme::getColors();
        g.fillAll(C.background);

        auto drawPanel = [&](juce::Rectangle<int> b, const juce::String& label)
        {
            g.setColour(C.surface);
            g.fillRoundedRectangle(b.toFloat(), 6.0f);
            g.setColour(C.border);
            g.drawRoundedRectangle(b.toFloat(), 6.0f, 1.0f);
            g.setColour(C.textSecondary);
            g.setFont(Theme::getBodyFont(11.0f).boldened());
            g.drawText(label, b.removeFromTop(18).reduced(8, 2),
                       juce::Justification::centredLeft);
        };

        for (auto& gp : groups)
            drawPanel(gp.bounds, gp.label);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        const int row1H = 220;
        auto top = area.removeFromTop(row1H);

        const int oscW = top.getWidth() / 3;
        layoutGroup(oscAGroup, top.removeFromLeft(oscW).reduced(4));
        layoutGroup(oscBGroup, top.removeFromLeft(oscW).reduced(4));
        layoutGroup(filterGroup, top.reduced(4));

        area.removeFromTop(8);
        const int row2H = 160;
        auto envRow = area.removeFromTop(row2H);
        const int envW = envRow.getWidth() / 4;
        layoutGroup(envAmpGroup,    envRow.removeFromLeft(envW).reduced(4));
        layoutGroup(envFiltGroup,   envRow.removeFromLeft(envW).reduced(4));
        layoutGroup(envModGroup,    envRow.removeFromLeft(envW).reduced(4));
        layoutGroup(fxGroup,        envRow.reduced(4));

        area.removeFromTop(8);
        layoutGroup(macroGroup, area.reduced(4));
    }

private:
    struct ControlGroup
    {
        juce::String label;
        juce::Rectangle<int> bounds;
        std::vector<std::unique_ptr<juce::Slider>> knobs;
        std::vector<std::unique_ptr<juce::Label>>  labels;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> atts;
    };

    void addKnob(ControlGroup& gp, const juce::String& name, const juce::String& paramId)
    {
        auto k = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                juce::Slider::TextBoxBelow);
        k->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
        addAndMakeVisible(*k);

        auto l = std::make_unique<juce::Label>();
        l->setText(name, juce::dontSendNotification);
        l->setFont(Theme::getBodyFont(10.5f));
        l->setColour(juce::Label::textColourId, Theme::getColors().textSecondary);
        l->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*l);

        if (apvts.getParameter(paramId))
            gp.atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                apvts, paramId, *k));

        gp.knobs.push_back(std::move(k));
        gp.labels.push_back(std::move(l));
    }

    void addOsc(const juce::String& title, const char* lvl, const char* det,
                const char* /*pw*/, ControlGroup& gp)
    {
        gp.label = title;
        addKnob(gp, "Level",  lvl);
        addKnob(gp, "Detune", det);
        groups.push_back(std::ref(gp));
    }

    void addFilter()
    {
        filterGroup.label = "FILTER";
        addKnob(filterGroup, "Cutoff", "filter1Cutoff");
        addKnob(filterGroup, "Reso",   "filter1Resonance");
        addKnob(filterGroup, "Drive",  "filter1Drive");
        addKnob(filterGroup, "Env",    "filter1EnvAmount");
        addKnob(filterGroup, "Key",    "filter1KeyTrack");
        groups.push_back(std::ref(filterGroup));
    }

    void addEnv(const juce::String& title, const char* prefix)
    {
        ControlGroup* gp = nullptr;
        if (juce::String(prefix) == "env1") gp = &envAmpGroup;
        else if (juce::String(prefix) == "env2") gp = &envFiltGroup;
        else gp = &envModGroup;
        gp->label = title + " ENV";
        const juce::String p(prefix);
        addKnob(*gp, "A", (p + "Attack").toRawUTF8());
        addKnob(*gp, "D", (p + "Decay").toRawUTF8());
        addKnob(*gp, "S", (p + "Sustain").toRawUTF8());
        addKnob(*gp, "R", (p + "Release").toRawUTF8());
        groups.push_back(std::ref(*gp));
    }

    void addFx()
    {
        fxGroup.label = "FX";
        addKnob(fxGroup, "Drive",   "fxDistortionAmount");
        addKnob(fxGroup, "Chorus",  "fxChorusMix");
        addKnob(fxGroup, "Delay",   "fxDelayMix");
        addKnob(fxGroup, "Reverb",  "fxReverbMix");
        groups.push_back(std::ref(fxGroup));
    }

    void addMacros()
    {
        macroGroup.label = "MACROS";
        for (int i = 1; i <= 8; ++i)
            addKnob(macroGroup, "M" + juce::String(i),
                    juce::String("macro" + juce::String(i)).toRawUTF8());
        groups.push_back(std::ref(macroGroup));
    }

    void layoutGroup(ControlGroup& gp, juce::Rectangle<int> bounds)
    {
        gp.bounds = bounds;
        auto inner = bounds.reduced(8).withTrimmedTop(20);
        const int n = static_cast<int>(gp.knobs.size());
        if (n == 0) return;
        const int kw = inner.getWidth() / n;
        for (int i = 0; i < n; ++i)
        {
            auto cell = inner.removeFromLeft(kw).reduced(4);
            gp.labels[i]->setBounds(cell.removeFromTop(14));
            gp.knobs[i]->setBounds(cell);
        }
    }

    juce::AudioProcessorValueTreeState& apvts;
    KnobLookAndFeel knobLAF;

    ControlGroup oscAGroup, oscBGroup, filterGroup;
    ControlGroup envAmpGroup, envFiltGroup, envModGroup, fxGroup, macroGroup;
    std::vector<std::reference_wrapper<ControlGroup>> groups;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainSynthPanel)
};
