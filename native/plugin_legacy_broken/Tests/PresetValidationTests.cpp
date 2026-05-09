// PresetValidationTests.cpp — basic JSON / schema validation smoke tests.
// Build target: a small console exe that links against juce_core only.
// Run manually after each preset edit to catch malformed JSON early.
#include <JuceHeader.h>
#include "../Source/Presets/PresetSchema.h"
#include "../Source/Presets/FactoryPresets.h"

static int failures = 0;
#define EXPECT(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main()
{
    auto& all = dida::factory::getAll();
    EXPECT(all.size() >= 30, "expected at least 30 factory presets");

    for (auto& p : all)
    {
        auto json = juce::JSON::parse(juce::String::fromUTF8(p.json));
        EXPECT(json.isObject(), p.name);
        EXPECT(json.hasProperty(dida::preset::key::presetName), p.name);
        EXPECT(json.hasProperty(dida::preset::key::presetVersion), p.name);
    }

    std::printf("PresetValidationTests: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
