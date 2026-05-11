#pragma once
#include <vector>
#include <string>
#include <functional>

struct ModRouting
{
    std::string sourceName;   // e.g. "env2", "lfo1", "velocity", "modwheel"
    std::string destName;     // e.g. "filter1Cutoff", "oscAPitch", "fxReverbMix"
    float amount = 0.0f;      // -1.0 to 1.0
    bool bipolar = true;
};

class ModMatrix
{
public:
    static constexpr int MAX_ROUTINGS = 12;

    ModMatrix() { routings.reserve(MAX_ROUTINGS); }

    bool addRouting(const ModRouting& r)
    {
        if (routings.size() >= MAX_ROUTINGS) return false;
        routings.push_back(r);
        return true;
    }

    void removeRouting(int index)
    {
        if (index >= 0 && index < static_cast<int>(routings.size()))
            routings.erase(routings.begin() + index);
    }

    void clearAll() { routings.clear(); }

    int getNumRoutings() const { return static_cast<int>(routings.size()); }
    ModRouting& getRouting(int i) { return routings[i]; }
    const ModRouting& getRouting(int i) const { return routings[i]; }

    // Process all routings: sourceValues map is filled per-block
    // Returns accumulated modulation for a given destination
    float getModulationFor(const std::string& dest, const std::function<float(const std::string&)>& getSourceValue) const
    {
        float total = 0.0f;
        for (const auto& r : routings)
        {
            if (r.destName == dest)
            {
                float srcVal = getSourceValue(r.sourceName);
                total += srcVal * r.amount;
            }
        }
        return total;
    }

private:
    std::vector<ModRouting> routings;
};
