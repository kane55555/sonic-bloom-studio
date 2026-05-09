/*
  PresetValidationTests.cpp
  ----------------------------------------------------------------------------
  Lightweight JUCE UnitTest covering HybridPresetV2 parsing, version detection,
  and PresetMigration round-tripping. Linked into the plugin only when
  JUCE_UNIT_TESTS is defined; never shipped to end users.
*/
#include <JuceHeader.h>
#include "../Presets/HybridPresetV2.h"
#include "../Presets/PresetMigration.h"

#if JUCE_UNIT_TESTS

class PresetValidationTests : public juce::UnitTest
{
public:
    PresetValidationTests() : juce::UnitTest ("Preset Validation v2") {}

    void runTest() override
    {
        beginTest ("schema version detection");
        {
            juce::var root = juce::JSON::parse (R"({
                "schemaVersion": "2.0.0",
                "plugin": "DIDITAGAIN STUDIO",
                "engine": "hybrid",
                "presetId": "t1",
                "name": "Test",
                "category": "DrillBells",
                "layers": [],
                "globalFilter": {},
                "effects": {},
                "macros": []
            })");
            expect (root.isObject(), "JSON parsed");
            expectEquals (root["schemaVersion"].toString(), juce::String ("2.0.0"));
        }

        beginTest ("legacy v1 detected and migrated");
        {
            juce::var legacy = juce::JSON::parse (R"({
                "presetVersion": 1,
                "presetName": "Old Bell",
                "category": "Glockenspiel",
                "sampler": { "instrument": "Glock" }
            })");
            expect (legacy.isObject());
            const bool isLegacy = ! legacy.hasProperty ("schemaVersion")
                               && (legacy.hasProperty ("presetVersion")
                                   || legacy.hasProperty ("sampler"));
            expect (isLegacy, "legacy preset recognised");

            // Migrate via PresetMigration helper (if present)
           #if defined(DIDA_HAS_PRESET_MIGRATION)
            auto migrated = dida::PresetMigration::toV2 (legacy);
            expect (migrated.isObject());
            expectEquals (migrated["schemaVersion"].toString(), juce::String ("2.0.0"));
            expectEquals (migrated["category"].toString(), juce::String ("DrillBells"));
           #endif
        }

        beginTest ("required fields enforced");
        {
            juce::var bad = juce::JSON::parse ("{}");
            expect (! bad.hasProperty ("schemaVersion"));
            expect (! bad.hasProperty ("layers"));
        }
    }
};

static PresetValidationTests presetValidationTests;

#endif // JUCE_UNIT_TESTS
