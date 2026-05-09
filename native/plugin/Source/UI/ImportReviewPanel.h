#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "../Presets/HybridPresetGenerator.h"
#include "../Presets/PresetManager.h"
#include "../DSP/SampleLibrary.h"

//==============================================================================
//  Drag-and-drop user sample importer (Phase 5 — user facing).
//  Users drop .wav/.aif/.mp3/.flac files onto the panel, edit Name / Category /
//  Root Note inline, and click "Create Preset" to build a 4-layer hybrid preset
//  via PresetTemplateFactory + HybridPresetGenerator. No Python required.
//==============================================================================
class ImportReviewPanel : public juce::Component,
                          public juce::FileDragAndDropTarget
{
public:
    ImportReviewPanel()
    {
        title.setText("IMPORT YOUR SAMPLES", juce::dontSendNotification);
        title.setFont(Theme::getHeadingFont(20.0f));
        title.setColour(juce::Label::textColourId, Theme::getColors().textPrimary);
        addAndMakeVisible(title);

        subtitle.setText("Drag any audio file (WAV / AIFF / MP3 / FLAC) anywhere on this panel, "
                         "edit the name, category and root note, then hit Create Preset.",
                         juce::dontSendNotification);
        subtitle.setFont(Theme::getBodyFont(13.0f));
        subtitle.setColour(juce::Label::textColourId, Theme::getColors().textSecondary);
        subtitle.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(subtitle);

        browseBtn.setButtonText("BROWSE FILES...");
        browseBtn.onClick = [this]() { openBrowseDialog(); };
        addAndMakeVisible(browseBtn);

        clearBtn.setButtonText("CLEAR QUEUE");
        clearBtn.onClick = [this]() { rows.clear(); resized(); repaint(); };
        addAndMakeVisible(clearBtn);

        createAllBtn.setButtonText("CREATE ALL PRESETS");
        createAllBtn.setColour(juce::TextButton::buttonColourId, Theme::getColors().accentPurple);
        createAllBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        createAllBtn.onClick = [this]() {
            for (int i = rows.size() - 1; i >= 0; --i)
                if (createPresetForRow(*rows[i]))
                    rows.remove(i);
            if (onPresetsCreated) onPresetsCreated();
            resized(); repaint();
        };
        addAndMakeVisible(createAllBtn);

        rescan.setButtonText("RESCAN PRESETS");
        rescan.onClick = [this]() { if (onRescan) onRescan(); };
        addAndMakeVisible(rescan);

        openInboxBtn.setButtonText("OPEN SAMPLES FOLDER");
        openInboxBtn.onClick = [this]() { if (onOpenInbox) onOpenInbox(); };
        addAndMakeVisible(openInboxBtn);

        viewport.setViewedComponent(&rowsHolder, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
    }

    //========== Drag & drop ==========
    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        for (auto& f : files) if (isAudioFile(f)) return true;
        return false;
    }

    void filesDropped(const juce::StringArray& files, int, int) override
    {
        for (auto& f : files) addFile(juce::File(f));
        dragOver = false;
        resized(); repaint();
    }

    void fileDragEnter(const juce::StringArray&, int, int) override { dragOver = true;  repaint(); }
    void fileDragExit (const juce::StringArray&)                    override { dragOver = false; repaint(); }

    //========== Layout ==========
    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::getColors().background);

        // Drop zone
        auto dz = dropZoneBounds();
        const auto col = dragOver ? Theme::getColors().accentPurple
                                  : Theme::getColors().border;
        g.setColour(Theme::getColors().surface);
        g.fillRoundedRectangle(dz.toFloat(), 8.0f);
        juce::Path dashed;
        dashed.addRoundedRectangle(dz.toFloat().reduced(2.0f), 8.0f);
        juce::PathStrokeType stroke(2.0f);
        float dashes[] { 8.0f, 6.0f };
        juce::Path dp;
        stroke.createDashedStroke(dp, dashed, dashes, 2);
        g.setColour(col);
        g.fillPath(dp);

        g.setColour(Theme::getColors().textPrimary);
        g.setFont(Theme::getHeadingFont(15.0f));
        g.drawText(rows.isEmpty() ? "DROP SAMPLES HERE"
                                  : juce::String(rows.size()) + " sample(s) ready — drop more or edit below",
                   dz, juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(16);
        title.setBounds(r.removeFromTop(28));
        subtitle.setBounds(r.removeFromTop(36));
        r.removeFromTop(8);

        // Top action row
        auto top = r.removeFromTop(34);
        browseBtn .setBounds(top.removeFromLeft(150).reduced(2));
        top.removeFromLeft(6);
        clearBtn  .setBounds(top.removeFromLeft(130).reduced(2));
        top.removeFromLeft(6);
        createAllBtn.setBounds(top.removeFromLeft(200).reduced(2));
        top.removeFromLeft(6);
        rescan    .setBounds(top.removeFromLeft(160).reduced(2));
        top.removeFromLeft(6);
        openInboxBtn.setBounds(top.removeFromLeft(200).reduced(2));
        r.removeFromTop(8);

        dropZoneRect = r.removeFromTop(80);
        r.removeFromTop(10);

        viewport.setBounds(r);
        const int rowH = 44;
        rowsHolder.setBounds(0, 0, r.getWidth() - 12, juce::jmax(r.getHeight(), rowH * rows.size() + 4));
        for (int i = 0; i < rows.size(); ++i)
            rows[i]->setBounds(0, i * rowH, rowsHolder.getWidth(), rowH);
    }

    //========== Public callbacks ==========
    std::function<void()> onRescan;
    std::function<void()> onOpenInbox;
    std::function<void()> onPresetsCreated;

    // Set by editor so we can resolve target folders + reload after import.
    PresetManager* presetManager = nullptr;

private:
    //========== Sample row UI ==========
    struct Row : public juce::Component
    {
        Row(ImportReviewPanel& ownerIn, const juce::File& f) : owner(ownerIn), file(f)
        {
            nameEdit.setText(f.getFileNameWithoutExtension(), juce::dontSendNotification);
            nameEdit.setFont(Theme::getBodyFont(13.0f));
            addAndMakeVisible(nameEdit);

            for (auto* c : kCategories) categoryBox.addItem(c, categoryBox.getNumItems() + 1);
            categoryBox.setSelectedId(detectCategoryId(f.getFileName()), juce::dontSendNotification);
            addAndMakeVisible(categoryBox);

            for (int m = 24; m <= 96; ++m) rootBox.addItem(midiName(m), m + 1);
            rootBox.setSelectedId(detectRootMidi(f.getFileName()) + 1, juce::dontSendNotification);
            addAndMakeVisible(rootBox);

            createBtn.setButtonText("CREATE");
            createBtn.setColour(juce::TextButton::buttonColourId, Theme::getColors().accentPurple);
            createBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            createBtn.onClick = [this]() {
                if (owner.createPresetForRow(*this))
                {
                    auto idx = owner.rows.indexOf(this);
                    if (idx >= 0) owner.rows.remove(idx);
                    if (owner.onPresetsCreated) owner.onPresetsCreated();
                    owner.resized(); owner.repaint();
                }
            };
            addAndMakeVisible(createBtn);

            removeBtn.setButtonText("X");
            removeBtn.onClick = [this]() {
                auto idx = owner.rows.indexOf(this);
                if (idx >= 0) owner.rows.remove(idx);
                owner.resized(); owner.repaint();
            };
            addAndMakeVisible(removeBtn);
        }

        void paint(juce::Graphics& g) override
        {
            g.setColour(Theme::getColors().surface);
            g.fillRoundedRectangle(getLocalBounds().reduced(2).toFloat(), 4.0f);
            g.setColour(Theme::getColors().textSecondary);
            g.setFont(Theme::getBodyFont(11.0f));
            g.drawText(file.getFileName(), 8, 2, getWidth() - 16, 14, juce::Justification::topLeft);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced(6, 4);
            r.removeFromTop(14); // file label
            nameEdit   .setBounds(r.removeFromLeft(220).reduced(2));
            r.removeFromLeft(4);
            categoryBox.setBounds(r.removeFromLeft(160).reduced(2));
            r.removeFromLeft(4);
            rootBox    .setBounds(r.removeFromLeft(80).reduced(2));
            r.removeFromLeft(4);
            removeBtn  .setBounds(r.removeFromRight(28).reduced(2));
            createBtn  .setBounds(r.removeFromRight(90).reduced(2));
        }

        ImportReviewPanel& owner;
        juce::File file;
        juce::TextEditor nameEdit;
        juce::ComboBox   categoryBox;
        juce::ComboBox   rootBox;
        juce::TextButton createBtn, removeBtn;
    };

    static constexpr const char* kCategories[5] = {
        "DrillBells", "Bass808", "FXRisers", "PainPianos", "Uncategorized"
    };

    static juce::String midiName(int m)
    {
        static const char* n[] { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        return juce::String(n[m % 12]) + juce::String(m / 12 - 1);
    }

    static int detectRootMidi(const juce::String& fn)
    {
        // matches _C5, -A#3, etc.
        juce::String s = fn.toUpperCase();
        for (int m = 24; m <= 96; ++m)
        {
            auto nm = midiName(m).toUpperCase();
            if (s.contains("_" + nm) || s.contains("-" + nm) || s.contains(" " + nm))
                return m;
        }
        return 60; // C4
    }

    static int detectCategoryId(const juce::String& fn)
    {
        auto s = fn.toLowerCase();
        if (s.contains("808") || s.contains("bass"))           return 2;
        if (s.contains("bell"))                                 return 1;
        if (s.contains("riser") || s.contains("fx") || s.contains("sweep")) return 3;
        if (s.contains("piano") || s.contains("keys"))          return 4;
        return 5;
    }

    static bool isAudioFile(const juce::String& path)
    {
        auto p = path.toLowerCase();
        return p.endsWith(".wav") || p.endsWith(".aif") || p.endsWith(".aiff")
            || p.endsWith(".mp3") || p.endsWith(".flac") || p.endsWith(".ogg");
    }

    juce::Rectangle<int> dropZoneBounds() const { return dropZoneRect; }

    void addFile(const juce::File& f)
    {
        if (! f.existsAsFile() || ! isAudioFile(f.getFullPathName())) return;
        auto* row = new Row(*this, f);
        rows.add(row);
        rowsHolder.addAndMakeVisible(row);
    }

    void openBrowseDialog()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select audio samples", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg");
        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::canSelectMultipleItems,
            [this](const juce::FileChooser& fc) {
                for (auto& f : fc.getResults()) addFile(f);
                resized(); repaint();
            });
    }

    static juce::String sanitiseForPath(const juce::String& s)
    {
        juce::String out;
        for (auto c : s)
        {
            if (juce::CharacterFunctions::isLetterOrDigit(c) || c == '-' || c == '_')
                out += juce::String::charToString(c);
            else if (c == ' ')
                out += "_";
        }
        return out.isEmpty() ? juce::String("Sample") : out;
    }

    bool createPresetForRow(Row& row)
    {
        if (presetManager == nullptr) return false;

        const juce::String name     = row.nameEdit.getText().trim().isNotEmpty()
                                        ? row.nameEdit.getText().trim()
                                        : row.file.getFileNameWithoutExtension();
        const juce::String category = kCategories[juce::jlimit(1, 5, row.categoryBox.getSelectedId()) - 1];
        const int rootMidi          = juce::jlimit(24, 96, row.rootBox.getSelectedId() - 1);
        const juce::String rootName = midiName(rootMidi);
        const juce::String safeName = sanitiseForPath(name);

        // Each imported preset gets its OWN instrument folder so SampleLibrary
        // loads it as a single dedicated multisample. Layout:
        //   Samples/Imported/<Category>/<SafeName>/<SafeName>_<RootNote>.<ext>
        //
        // The note suffix is required: SampleLibrary skips any file whose name
        // doesn't match the "_C5" / "_F#3" convention. Without this, the
        // instrument loads empty and the voice falls back to a generic sine.
        // Samples MUST live under the same root SampleLibrary reads from
        // (Documents/DIDITAGAIN STUDIO/Samples), NOT under the user-preset
        // folder (which is in AppData on Windows). Mismatch = silent fallback.
        auto samplesRoot = dida::SampleLibrary::getSamplesRoot();
        samplesRoot.createDirectory();
        auto samplesDir  = samplesRoot.getChildFile("Imported")
                                      .getChildFile(category)
                                      .getChildFile(safeName);
        samplesDir.createDirectory();

        const juce::String destStem = safeName + "_" + rootName;
        auto destSample = samplesDir.getNonexistentChildFile(destStem,
                                                             row.file.getFileExtension(), true);
        if (! row.file.copyFileTo(destSample))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                "Import failed", "Could not copy " + row.file.getFileName());
            return false;
        }

        // Build a "Samples/Imported/<Cat>/<Name>/<File>" relative path that
        // PresetManager strips back into the SampleLibrary instrument name.
        auto sampleRel = juce::String("Samples/")
            + destSample.getRelativePathFrom(samplesRoot).replaceCharacter('\\', '/');

        dida::preset::HybridPresetGenerator::Inputs in;
        in.category         = category;
        in.presetName       = name;
        in.sampleRelPath    = sampleRel;
        in.originalFileName = row.file.getFileName();
        in.rootNote         = rootName;
        in.rootMidi         = rootMidi;
        in.pitchTracking    = ! (category == "FXRisers");
        in.oneShotMode      = (category == "FXRisers");
        in.needsReview      = false;
        in.rootNoteSource   = "manual";

        auto preset = dida::preset::HybridPresetGenerator::generate(in);
        preset.name = name;

        auto presetDir  = userPresets.getChildFile(category);
        presetDir.createDirectory();
        auto presetFile = presetDir.getNonexistentChildFile(name, ".didasynthpreset", true);

        auto json = dida::preset::HybridPresetGenerator::toJsonString(preset);
        if (! presetFile.replaceWithText(json))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                "Import failed", "Could not write preset " + presetFile.getFullPathName());
            return false;
        }

        // Drop any cached multisample so the new folder is picked up immediately.
        dida::SampleLibrary::invalidateCache();
        presetManager->scanPresetDirectory();
        return true;
    }

    juce::Label title, subtitle;
    juce::TextButton browseBtn, clearBtn, createAllBtn, rescan, openInboxBtn;

    juce::Viewport  viewport;
    juce::Component rowsHolder;
    juce::OwnedArray<Row> rows;

    juce::Rectangle<int> dropZoneRect;
    bool dragOver = false;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImportReviewPanel)
};
