#include "PresetIndex.h"

namespace dida { namespace preset {

static juce::File indexFile(const juce::File& root)
{
    return root.getChildFile("Presets").getChildFile("index.json");
}

std::vector<PresetIndexEntry> PresetIndex::load(const juce::File& root)
{
    std::vector<PresetIndexEntry> out;
    auto f = indexFile(root);
    if (! f.existsAsFile()) return out;
    auto json = juce::JSON::parse(f);
    auto* arr = json.getArray();
    if (! arr) return out;
    for (auto& e : *arr) {
        if (! e.isObject()) continue;
        PresetIndexEntry ent;
        ent.presetId   = e.getProperty("presetId", "").toString();
        ent.name       = e.getProperty("name", "").toString();
        ent.bank       = e.getProperty("bank", "User").toString();
        ent.category   = e.getProperty("category", "Uncategorized").toString();
        if (auto* ta = e.getProperty("tags", juce::var()).getArray())
            for (auto& t : *ta) ent.tags.add(t.toString());
        ent.presetPath  = e.getProperty("presetPath", "").toString();
        ent.samplePath  = e.getProperty("samplePath", "").toString();
        ent.createdAt   = e.getProperty("createdAt", "").toString();
        ent.modifiedAt  = e.getProperty("modifiedAt", "").toString();
        ent.favorite    = (bool) e.getProperty("favorite", false);
        ent.userEdited  = (bool) e.getProperty("userEdited", false);
        ent.needsReview = (bool) e.getProperty("needsReview", false);
        out.push_back(ent);
    }
    return out;
}

bool PresetIndex::save(const juce::File& root,
                       const std::vector<PresetIndexEntry>& entries)
{
    auto f = indexFile(root);
    f.getParentDirectory().createDirectory();
    juce::Array<juce::var> arr;
    for (auto& e : entries) {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty("presetId", e.presetId);
        o->setProperty("name", e.name);
        o->setProperty("bank", e.bank);
        o->setProperty("category", e.category);
        juce::Array<juce::var> tags;
        for (auto& t : e.tags) tags.add(t);
        o->setProperty("tags", tags);
        o->setProperty("presetPath", e.presetPath);
        o->setProperty("samplePath", e.samplePath);
        o->setProperty("createdAt", e.createdAt);
        o->setProperty("modifiedAt", e.modifiedAt);
        o->setProperty("favorite", e.favorite);
        o->setProperty("userEdited", e.userEdited);
        o->setProperty("needsReview", e.needsReview);
        arr.add(juce::var(o.get()));
    }
    return f.replaceWithText(juce::JSON::toString(juce::var(arr)));
}

void PresetIndex::upsert(std::vector<PresetIndexEntry>& list, const PresetIndexEntry& entry)
{
    for (auto& e : list)
        if (e.presetPath == entry.presetPath) { e = entry; return; }
    list.push_back(entry);
}

std::vector<PresetIndexEntry>
PresetIndex::filterByCategory(const std::vector<PresetIndexEntry>& list, const juce::String& category)
{
    std::vector<PresetIndexEntry> out;
    for (auto& e : list)
        if (e.category.equalsIgnoreCase(category)) out.push_back(e);
    return out;
}

std::vector<PresetIndexEntry>
PresetIndex::filterNeedsReview(const std::vector<PresetIndexEntry>& list)
{
    std::vector<PresetIndexEntry> out;
    for (auto& e : list) if (e.needsReview) out.push_back(e);
    return out;
}

}} // namespace dida::preset
