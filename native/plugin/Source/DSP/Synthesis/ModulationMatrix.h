#pragma once
//==============================================================================
//  ModulationMatrix.h — Thin alias over the existing ModMatrix that adds a
//  named-source registry so sources (LFO1, env2, velocity, modwheel...) can
//  be supplied each block without per-routing lambdas.
//
//  Kept tiny on purpose: the heavy lifting still lives in DSP/ModMatrix.h.
//==============================================================================
#include "../ModMatrix.h"
#include <unordered_map>
#include <string>

namespace dida {

class ModulationMatrix
{
public:
    void setSource(const std::string& name, float value) { sources[name] = value; }
    void clearSources() { sources.clear(); }

    void addRouting(const ModRouting& r) { mat.addRouting(r); }
    void clearRoutings() { mat.clearAll(); }

    float modFor(const std::string& dest) const
    {
        return mat.getModulationFor(dest, [this](const std::string& s) {
            auto it = sources.find(s);
            return it == sources.end() ? 0.0f : it->second;
        });
    }

private:
    ModMatrix mat;
    std::unordered_map<std::string, float> sources;
};

} // namespace dida
