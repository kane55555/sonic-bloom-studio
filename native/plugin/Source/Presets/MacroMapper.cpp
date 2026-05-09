#include "MacroMapper.h"

namespace dida { namespace preset {

static juce::AudioProcessorParameterWithID* findParam(juce::AudioProcessor& proc, const juce::String& id)
{
    for (auto* p : proc.getParameters())
        if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            if (w->paramID == id)
                return w;
    return nullptr;
}

// Translate a V2 path into one or more APVTS parameter IDs.
static juce::StringArray pathToParamIDs(const juce::String& path)
{
    if (path == "globalFilter.cutoff")        return { "filter1Cutoff" };
    if (path == "globalFilter.resonance")     return { "filter1Resonance" };
    if (path == "effects.reverb.mix")         return { "fxReverbMix" };
    if (path == "effects.reverb.size")        return { "fxReverbSize" };
    if (path == "effects.delay.mix")          return { "fxDelayMix" };
    if (path == "effects.delay.feedback")     return { "fxDelayFeedback" };
    if (path == "effects.delay.time")         return { "fxDelayTime" };
    if (path == "effects.saturation.drive")   return { "fxDistortionAmount" };
    if (path == "effects.chorus.mix")         return { "fxChorusMix" };
    if (path == "effects.width.amount")       return { "fxPhaserMix" }; // best-fit: phaser/width slot
    if (path == "glideTime")                  return { "glideTime" };
    if (path == "monoMode")                   return { "monoMode" };
    if (path == "layers[0].volume" || path == "layers[1].volume")
        return { "oscALevel" };
    if (path == "layers[2].volume")           return { "oscBLevel" };
    if (path == "layers[3].volume")           return { "noiseLevel" };
    if (path == "layers[4].volume")           return { "subOscLevel" };
    return {};
}

void MacroMapper::buildFrom(const HybridPresetV2& preset, juce::AudioProcessor& processor)
{
    bindings.clear();
    int idx = 1;
    for (const auto& m : preset.macros)
    {
        MacroBinding b;
        b.macroIndex = idx++;
        b.displayName = m.name;
        for (const auto& t : m.targets)
        {
            auto ids = pathToParamIDs(t.path);
            for (const auto& pid : ids)
            {
                if (auto* param = findParam(processor, pid))
                {
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(param))
                    {
                        ResolvedTarget rt;
                        rt.paramID = pid;
                        rt.minNorm = juce::jlimit(0.0f, 1.0f, r->convertTo0to1(t.min));
                        rt.maxNorm = juce::jlimit(0.0f, 1.0f, r->convertTo0to1(t.max));
                        if (rt.maxNorm < rt.minNorm) std::swap(rt.minNorm, rt.maxNorm);
                        b.targets.push_back(rt);
                    }
                }
            }
        }
        if (! b.targets.empty() || b.displayName.isNotEmpty())
            bindings.push_back(std::move(b));
    }
}

void MacroMapper::apply(juce::AudioProcessor& processor) const
{
    if (bindings.empty()) return;
    for (const auto& b : bindings)
    {
        const juce::String macroId = "macro" + juce::String(b.macroIndex);
        auto* mp = findParam(processor, macroId);
        if (mp == nullptr) continue;
        const float v = mp->getValue(); // 0..1
        for (const auto& t : b.targets)
        {
            if (auto* dst = findParam(processor, t.paramID))
            {
                const float mapped = t.minNorm + (t.maxNorm - t.minNorm) * v;
                dst->setValue(juce::jlimit(0.0f, 1.0f, mapped));
            }
        }
    }
}

juce::String MacroMapper::getMacroName(int macroIndex) const
{
    for (const auto& b : bindings)
        if (b.macroIndex == macroIndex)
            return b.displayName;
    return {};
}

}} // namespace dida::preset
