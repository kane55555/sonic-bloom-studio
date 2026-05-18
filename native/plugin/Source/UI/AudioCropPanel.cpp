#include "AudioCropPanel.h"
#include "Theme.h"
#include "../DSP/SampleLibrary.h"

namespace {
    static const char* kCategories[] = {
        "All Imported Samples", "Current Preset Sample",
        "Pianos", "Keys", "Guitars", "Strings", "Pads", "Bells", "Plucks",
        "Leads", "Bass", "Synths", "Choirs", "Brass", "Winds",
        "Drums", "FX", "Imported", "Uncategorized"
    };

    static const char* kEditableCats[] = {
        "Pianos", "Keys", "Guitars", "Strings", "Pads", "Bells", "Plucks",
        "Leads", "Bass", "Synths", "Choirs", "Brass", "Winds",
        "Drums", "FX", "Imported", "Uncategorized"
    };

    static juce::File importedRoot()
    {
        return dida::SampleLibrary::getSamplesRoot().getChildFile("Imported");
    }
    static juce::File userRoot()
    {
        return dida::SampleLibrary::getSamplesRoot().getChildFile("User");
    }
    // Drop-folder presets live under <Documents>/DIDITAGAIN STUDIO/Samples/Presets/User/<Cat>/.
    // Surface them in the crop browser too.
    static juce::File presetDropsRoot()
    {
        return dida::SampleLibrary::getSamplesRoot()
            .getChildFile("Presets")
            .getChildFile("User");
    }

    static juce::String midiToNoteName(int midi)
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        const int n = midi % 12;
        const int oct = (midi / 12) - 1;
        return juce::String(names[n]) + juce::String(oct);
    }
}

// =============================================================================
// metadata sidecar JSON
// =============================================================================
juce::File AudioCropPanel::metaFileFor(const juce::File& audio)
{
    return audio.withFileExtension("dida-crop.json");
}

AudioCropPanel::CropMeta AudioCropPanel::readMeta(const juce::File& audio)
{
    CropMeta m;
    m.sourcePath       = audio.getFullPathName();
    m.originalFileName = audio.getFileName();
    m.sampleId         = audio.getFileNameWithoutExtension();
    m.category         = inferCategory(audio);
    m.rootMidi         = inferRootMidi(audio.getFileNameWithoutExtension(), m.rootNote);

    auto mf = metaFileFor(audio);
    if (mf.existsAsFile())
    {
        auto v = juce::JSON::parse(mf);
        if (auto* obj = v.getDynamicObject())
        {
            auto get = [&](const char* k, auto& dst) {
                if (obj->hasProperty(k)) dst = obj->getProperty(k);
            };
            juce::var tmp;
            if (obj->hasProperty("sampleId"))         m.sampleId        = obj->getProperty("sampleId").toString();
            if (obj->hasProperty("category"))         m.category        = obj->getProperty("category").toString();
            if (obj->hasProperty("rootNote"))         m.rootNote        = obj->getProperty("rootNote").toString();
            if (obj->hasProperty("rootMidi"))         m.rootMidi        = (int)   obj->getProperty("rootMidi");
            if (obj->hasProperty("cropStart"))        m.cropStart       = (double)obj->getProperty("cropStart");
            if (obj->hasProperty("cropEnd"))          m.cropEnd         = (double)obj->getProperty("cropEnd");
            if (obj->hasProperty("loopStart"))        m.loopStart       = (double)obj->getProperty("loopStart");
            if (obj->hasProperty("loopEnd"))          m.loopEnd         = (double)obj->getProperty("loopEnd");
            if (obj->hasProperty("loopCrossfadeMs"))  m.loopCrossfadeMs = (double)obj->getProperty("loopCrossfadeMs");
            if (obj->hasProperty("autoLoop"))         m.autoLoop        = (bool)  obj->getProperty("autoLoop");
            if (obj->hasProperty("oneShotMode"))      m.oneShotMode     = (bool)  obj->getProperty("oneShotMode");
            if (obj->hasProperty("pitchTracking"))    m.pitchTracking   = (bool)  obj->getProperty("pitchTracking");
            if (obj->hasProperty("needsReview"))      m.needsReview     = (bool)  obj->getProperty("needsReview");
            if (obj->hasProperty("dateModified"))     m.dateModified    = obj->getProperty("dateModified").toString();
            if (obj->hasProperty("loopEnabled"))      m.loopEnabled     = (bool)  obj->getProperty("loopEnabled");
        }
    }
    else
    {
        // Defaults based on category.
        const auto cat = m.category;
        if (cat == "FX" || cat == "Drums")
        { m.autoLoop = false; m.oneShotMode = true;  m.pitchTracking = false; }
        else if (cat == "Bass")
        { m.autoLoop = false; m.oneShotMode = false; m.pitchTracking = true;  }
        else
        { m.autoLoop = true;  m.oneShotMode = false; m.pitchTracking = true;  }
        m.needsReview = true;
    }
    return m;
}

void AudioCropPanel::writeMeta(const CropMeta& m)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("sampleId",        m.sampleId);
    obj->setProperty("sourcePath",      m.sourcePath);
    obj->setProperty("originalFileName",m.originalFileName);
    obj->setProperty("category",        m.category);
    obj->setProperty("rootNote",        m.rootNote);
    obj->setProperty("rootMidi",        m.rootMidi);
    obj->setProperty("cropStart",       m.cropStart);
    obj->setProperty("cropEnd",         m.cropEnd);
    obj->setProperty("loopEnabled",     m.loopEnabled);
    obj->setProperty("loopStart",       m.loopStart);
    obj->setProperty("loopEnd",         m.loopEnd);
    obj->setProperty("loopCrossfadeMs", m.loopCrossfadeMs);
    obj->setProperty("autoLoop",        m.autoLoop);
    obj->setProperty("oneShotMode",     m.oneShotMode);
    obj->setProperty("pitchTracking",   m.pitchTracking);
    obj->setProperty("needsReview",     m.needsReview);
    obj->setProperty("dateModified",    juce::Time::getCurrentTime().toISO8601(true));

    juce::var v(obj);
    auto file = metaFileFor(juce::File(m.sourcePath));
    file.replaceWithText(juce::JSON::toString(v, true));
}

juce::String AudioCropPanel::inferCategory(const juce::File& f)
{
    auto parent = f.getParentDirectory().getFileName().toLowerCase();
    if (parent.contains("piano"))  return "Pianos";
    if (parent.contains("key") || parent.contains("organ") || parent.contains("ep")) return "Keys";
    if (parent.contains("guitar")) return "Guitars";
    if (parent.contains("string") || parent.contains("violin") || parent.contains("cello")) return "Strings";
    if (parent.contains("pad"))    return "Pads";
    if (parent.contains("bell"))   return "Bells";
    if (parent.contains("pluck"))  return "Plucks";
    if (parent.contains("lead") || parent.contains("alien")) return "Leads";
    if (parent.contains("bass") || parent.contains("808")) return "Bass";
    if (parent.contains("synth"))  return "Synths";
    if (parent.contains("choir") || parent.contains("vox") || parent.contains("vocal")) return "Choirs";
    if (parent.contains("brass"))  return "Brass";
    if (parent.contains("wind") || parent.contains("flute")) return "Winds";
    if (parent.contains("drum") || parent.contains("perc")) return "Drums";
    if (parent.contains("fx") || parent.contains("riser") || parent.contains("texture")) return "FX";
    return "Uncategorized";
}

int AudioCropPanel::inferRootMidi(const juce::String& name, juce::String& outNoteText)
{
    // Look for _C3, _F#3, _Bb4 etc.
    auto upper = name;
    juce::StringArray notes { "C","C#","Db","D","D#","Eb","E","F","F#","Gb","G","G#","Ab","A","A#","Bb","B" };
    for (int oct = -1; oct <= 9; ++oct)
    {
        for (auto& nn : notes)
        {
            juce::String token = "_" + nn + juce::String(oct);
            if (upper.containsIgnoreCase(token))
            {
                static const std::unordered_map<std::string,int> table {
                    {"C",0},{"C#",1},{"Db",1},{"D",2},{"D#",3},{"Eb",3},
                    {"E",4},{"F",5},{"F#",6},{"Gb",6},{"G",7},{"G#",8},
                    {"Ab",8},{"A",9},{"A#",10},{"Bb",10},{"B",11}
                };
                auto it = table.find(nn.toStdString());
                int semis = (it != table.end()) ? it->second : 0;
                outNoteText = nn + juce::String(oct);
                return (oct + 1) * 12 + semis;
            }
        }
    }
    outNoteText = "C5";
    return 72;
}

// =============================================================================
// list model
// =============================================================================
int AudioCropPanel::ListModel::getNumRows()
{
    return (int) owner.visibleIndices.size();
}

void AudioCropPanel::ListModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel)
{
    if (row < 0 || row >= (int) owner.visibleIndices.size()) return;
    auto& m = owner.samples[(size_t) owner.visibleIndices[(size_t) row]];
    auto& C = Theme::getColors();
    g.fillAll(sel ? juce::Colour(0x33ffffff).overlaidWith(C.accentTeal.withAlpha(0.18f))
                  : C.surface);
    g.setColour(C.textPrimary);
    g.setFont(Theme::getBodyFont(12.0f));
    g.drawText(m.originalFileName, 8, 4, w - 16, 16, juce::Justification::centredLeft, true);
    g.setColour(C.textSecondary);
    g.setFont(Theme::getBodyFont(10.0f));
    juce::String tags = m.category + "  |  " + m.rootNote;
    if (m.autoLoop)    tags += "  |  Loop";
    if (m.needsReview) tags += "  |  Needs Review";
    g.drawText(tags, 8, 22, w - 16, 14, juce::Justification::centredLeft, true);
}

void AudioCropPanel::ListModel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) owner.visibleIndices.size()) return;
    owner.selectIndex(owner.visibleIndices[(size_t) row]);
}

// =============================================================================
// Waveform display
// =============================================================================
void AudioCropPanel::Waveform::loadFor(const juce::File& f)
{
    buffer.setSize(0, 0);
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    if (auto* reader = fm.createReaderFor(f))
    {
        std::unique_ptr<juce::AudioFormatReader> r(reader);
        sampleRate = r->sampleRate;
        const int numCh = (int) juce::jmin((unsigned)2, r->numChannels);
        const int len   = (int) juce::jmin<int64_t>(r->lengthInSamples, 44100 * 30); // cap at 30s for display
        buffer.setSize(numCh, len);
        r->read(&buffer, 0, len, 0, true, numCh > 1);
    }
    viewStart = 0.0;
    viewSpan  = 1.0;
    repaint();
}

double AudioCropPanel::Waveform::screenToFrac(int x) const
{
    const double f = (double) x / (double) juce::jmax(1, getWidth());
    return juce::jlimit(0.0, 1.0, viewStart + juce::jlimit(0.0, 1.0, f) * viewSpan);
}

void AudioCropPanel::Waveform::zoomBy(double factor, double anchorFrac)
{
    const double newSpan = juce::jlimit(0.0005, 1.0, viewSpan * factor);
    // keep anchorFrac (in buffer space) stationary on screen
    const double anchorScreen = (anchorFrac - viewStart) / juce::jmax(1e-9, viewSpan); // 0..1
    double newStart = anchorFrac - anchorScreen * newSpan;
    newStart = juce::jlimit(0.0, 1.0 - newSpan, newStart);
    viewStart = newStart;
    viewSpan  = newSpan;
    repaint();
}

void AudioCropPanel::Waveform::resetZoom()
{
    viewStart = 0.0;
    viewSpan  = 1.0;
    repaint();
}

void AudioCropPanel::Waveform::panBy(double deltaFrac)
{
    viewStart = juce::jlimit(0.0, 1.0 - viewSpan, viewStart + deltaFrac);
    repaint();
}

void AudioCropPanel::Waveform::paint(juce::Graphics& g)
{
    auto& C = Theme::getColors();
    g.fillAll(juce::Colour(0xff0c0f12));

    const int w = getWidth(), h = getHeight();

    if (buffer.getNumSamples() == 0)
    {
        g.setColour(C.textSecondary);
        g.setFont(Theme::getBodyFont(13.0f));
        g.drawText("Select a sample to load its waveform", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Waveform — only render samples inside view window
    g.setColour(C.accentTeal.withAlpha(0.55f));
    const int n = buffer.getNumSamples();
    const int viewS0 = (int) juce::jlimit(0.0, (double) (n - 1), viewStart * n);
    const int viewLen = juce::jmax(1, (int) (viewSpan * n));
    const int step = juce::jmax(1, viewLen / w);
    auto* d = buffer.getReadPointer(0);
    for (int x = 0; x < w; ++x)
    {
        float lo = 1.f, hi = -1.f;
        const int s0 = viewS0 + x * step;
        for (int i = 0; i < step && (s0 + i) < n; ++i)
        {
            float s = d[s0 + i];
            if (s < lo) lo = s; if (s > hi) hi = s;
        }
        const int yHi = (int) ((1.f - (hi + 1.f) * 0.5f) * h);
        const int yLo = (int) ((1.f - (lo + 1.f) * 0.5f) * h);
        g.drawVerticalLine(x, (float) yHi, (float) yLo);
    }

    if (owner.selectedIndex < 0) return;
    auto& m = owner.samples[(size_t) owner.selectedIndex];

    auto fracToX = [&](double frac) -> int {
        return (int) (((frac - viewStart) / juce::jmax(1e-9, viewSpan)) * w);
    };

    // Loop region fill
    g.setColour(C.accentTeal.withAlpha(0.12f));
    const int lx0 = fracToX(m.loopStart);
    const int lx1 = fracToX(m.loopEnd);
    g.fillRect(lx0, 0, lx1 - lx0, h);

    // Crop dim
    g.setColour(juce::Colour(0xaa000000));
    const int cx0 = fracToX(m.cropStart);
    const int cx1 = fracToX(m.cropEnd);
    if (cx0 > 0) g.fillRect(0, 0, cx0, h);
    if (cx1 < w) g.fillRect(cx1, 0, w - cx1, h);

    auto marker = [&](double frac, juce::Colour col, const juce::String& label) {
        int x = fracToX(frac);
        if (x < -8 || x > w + 8) return;
        g.setColour(col); g.drawVerticalLine(x, 0.0f, (float) h);
        g.fillRect(x - 4, 0, 8, 6);
        g.setColour(C.textPrimary);
        g.setFont(Theme::getBodyFont(10.0f));
        g.drawText(label, x + 4, 2, 80, 12, juce::Justification::centredLeft);
    };
    marker(m.cropStart, C.accentTeal,                "Start");
    marker(m.cropEnd,   C.accentTeal,                "End");
    marker(m.loopStart, C.accentTeal.brighter(0.4f), "L<");
    marker(m.loopEnd,   C.accentTeal.brighter(0.4f), "L>");
}

void AudioCropPanel::Waveform::mouseDown(const juce::MouseEvent& e)
{
    // Right-click or middle-click = pan
    if (e.mods.isRightButtonDown() || e.mods.isMiddleButtonDown())
    {
        panning = true;
        panLastX = e.x;
        dragHandle = -1;
        return;
    }
    panning = false;

    if (owner.selectedIndex < 0) { dragHandle = -1; return; }
    auto& m = owner.samples[(size_t) owner.selectedIndex];
    const double frac = screenToFrac(e.x);
    const double dists[4] = {
        std::abs(frac - m.cropStart),
        std::abs(frac - m.cropEnd),
        std::abs(frac - m.loopStart),
        std::abs(frac - m.loopEnd)
    };
    int best = 0; for (int i = 1; i < 4; ++i) if (dists[i] < dists[best]) best = i;
    dragHandle = best;
    mouseDrag(e);
}

void AudioCropPanel::Waveform::mouseDrag(const juce::MouseEvent& e)
{
    if (panning)
    {
        const double dx = (double) (panLastX - e.x) / (double) juce::jmax(1, getWidth());
        panBy(dx * viewSpan);
        panLastX = e.x;
        return;
    }
    if (dragHandle < 0 || owner.selectedIndex < 0) return;
    auto& m = owner.samples[(size_t) owner.selectedIndex];
    const double frac = screenToFrac(e.x);
    switch (dragHandle)
    {
        case 0: m.cropStart = juce::jmin(frac, m.cropEnd);    break;
        case 1: m.cropEnd   = juce::jmax(frac, m.cropStart);  break;
        case 2: m.loopStart = juce::jmin(frac, m.loopEnd);    break;
        case 3: m.loopEnd   = juce::jmax(frac, m.loopStart);  break;
    }
    owner.pushUiFromMeta(m);
    repaint();
}

void AudioCropPanel::Waveform::mouseWheelMove(const juce::MouseEvent& e,
                                              const juce::MouseWheelDetails& w)
{
    const double anchor = screenToFrac(e.x);
    const double factor = (w.deltaY > 0.0f) ? 0.8 : 1.25;
    zoomBy(factor, anchor);
}

// =============================================================================
// AudioCropPanel
// =============================================================================
AudioCropPanel::AudioCropPanel()
{
    auto& C = Theme::getColors();

    listModel = std::make_unique<ListModel>(*this);
    waveform  = std::make_unique<Waveform>(*this);

    formatManager.registerBasicFormats();

    // ---- left ----
    for (auto* c : kCategories) categoryFilter.addItem(c, categoryFilter.getNumItems() + 1);
    categoryFilter.setSelectedId(1, juce::dontSendNotification);
    categoryFilter.onChange = [this]() {
        filterCategory = categoryFilter.getText();
        applyFilter();
    };
    addAndMakeVisible(categoryFilter);

    searchBox.setTextToShowWhenEmpty("Search samples...", C.textSecondary);
    searchBox.onTextChange = [this]() { searchText = searchBox.getText(); applyFilter(); };
    addAndMakeVisible(searchBox);

    sampleList.setModel(listModel.get());
    sampleList.setRowHeight(44);
    sampleList.setColour(juce::ListBox::backgroundColourId, C.background);
    addAndMakeVisible(sampleList);

    importButton.onClick = [this]() {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Import sample(s) into Audio Crop", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                           | juce::FileBrowserComponent::canSelectMultipleItems
                           | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto dest = importedRoot().getChildFile("UserCrop");
                dest.createDirectory();
                for (auto& f : fc.getResults())
                    f.copyFileTo(dest.getChildFile(f.getFileName()));
                rescan();
            });
    };
    rescanButton.onClick  = [this]() { rescan(); };
    saveAllButton.onClick = [this]() {
        for (auto& m : samples) writeMeta(m);
    };
    addAndMakeVisible(importButton);
    addAndMakeVisible(rescanButton);
    addAndMakeVisible(saveAllButton);

    // ---- center ----
    addAndMakeVisible(*waveform);
    addAndMakeVisible(zoomIn);
    addAndMakeVisible(zoomOut);
    addAndMakeVisible(resetView);
    addAndMakeVisible(snapZero);

    zoomIn.onClick    = [this]() { if (waveform) waveform->zoomBy(0.5,
        waveform->viewStart + waveform->viewSpan * 0.5); };
    zoomOut.onClick   = [this]() { if (waveform) waveform->zoomBy(2.0,
        waveform->viewStart + waveform->viewSpan * 0.5); };
    resetView.onClick = [this]() { if (waveform) waveform->resetZoom(); };

    // ---- right controls ----
    auto setupSlider = [this](juce::Slider& s, juce::Label& l, const juce::String& name,
                              double min, double max, double step) {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 18);
        s.setRange(min, max, step);
        s.onValueChange = [this]() {
            if (selectedIndex < 0) return;
            auto& m = samples[(size_t) selectedIndex];
            pullMetaFromUi(m);
            waveform->repaint();
        };
        l.setText(name, juce::dontSendNotification);
        l.setFont(Theme::getBodyFont(11.0f));
        addAndMakeVisible(s);
        addAndMakeVisible(l);
    };
    setupSlider(startSlider,    startLabel,    "Start",      0.0, 1.0, 0.001);
    setupSlider(endSlider,      endLabel,      "End",        0.0, 1.0, 0.001);
    setupSlider(loopStartSlider,loopStartLabel,"Loop Start", 0.0, 1.0, 0.001);
    setupSlider(loopEndSlider,  loopEndLabel,  "Loop End",   0.0, 1.0, 0.001);
    setupSlider(smoothSlider,   smoothLabel,   "Smooth Loop (ms)", 0.0, 50.0, 1.0);

    auto setupToggle = [this](juce::ToggleButton& b) {
        b.setColour(juce::ToggleButton::tickColourId, Theme::getColors().accentTeal);
        b.onClick = [this]() {
            if (selectedIndex < 0) return;
            pullMetaFromUi(samples[(size_t) selectedIndex]);
        };
        addAndMakeVisible(b);
    };
    setupToggle(autoLoopBtn);
    setupToggle(oneShotBtn);
    setupToggle(playAcrossBtn);

    for (int midi = 24; midi <= 96; ++midi)
        rootNoteBox.addItem(midiToNoteName(midi), midi + 1);
    rootNoteBox.onChange = [this]() {
        if (selectedIndex < 0) return;
        auto& m = samples[(size_t) selectedIndex];
        m.rootMidi = rootNoteBox.getSelectedId() - 1;
        m.rootNote = midiToNoteName(m.rootMidi);
    };
    addAndMakeVisible(rootNoteBox);

    for (auto* c : kEditableCats) categoryBox.addItem(c, categoryBox.getNumItems() + 1);
    categoryBox.onChange = [this]() {
        if (selectedIndex < 0) return;
        samples[(size_t) selectedIndex].category = categoryBox.getText();
        sampleList.updateContent();
    };
    addAndMakeVisible(categoryBox);

    previewBtn.onClick = [this]() { startPreview(); };
    saveBtn.onClick    = [this]() { persistSelected(); };
    resetBtn.onClick   = [this]() { resetSelectedToOriginal(); };
    addAndMakeVisible(previewBtn);
    addAndMakeVisible(saveBtn);
    addAndMakeVisible(resetBtn);

    rescan();
}

AudioCropPanel::~AudioCropPanel()
{
    stopPreview();
    sampleList.setModel(nullptr);
}

void AudioCropPanel::rescan()
{
    samples.clear();
    auto scanFolder = [this](const juce::File& root) {
        if (! root.isDirectory()) return;
        auto files = root.findChildFiles(juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        for (auto& f : files) samples.push_back(readMeta(f));
    };
    scanFolder(importedRoot());
    scanFolder(userRoot());
    scanFolder(presetDropsRoot());
    applyFilter();
    if (! samples.empty()) selectIndex(0);
}

void AudioCropPanel::applyFilter()
{
    visibleIndices.clear();
    const auto search = searchText.toLowerCase();
    for (size_t i = 0; i < samples.size(); ++i)
    {
        const auto& m = samples[i];
        if (filterCategory != "All Imported Samples"
            && filterCategory != "Current Preset Sample"
            && m.category    != filterCategory) continue;
        if (search.isNotEmpty() && ! m.originalFileName.toLowerCase().contains(search)) continue;
        visibleIndices.push_back((int) i);
    }
    sampleList.updateContent();
    sampleList.repaint();
}

void AudioCropPanel::selectIndex(int newIndex)
{
    selectedIndex = newIndex;
    if (newIndex < 0 || newIndex >= (int) samples.size()) return;
    auto& m = samples[(size_t) newIndex];
    waveform->loadFor(juce::File(m.sourcePath));
    pushUiFromMeta(m);
    repaint();
}

void AudioCropPanel::pushUiFromMeta(const CropMeta& m)
{
    startSlider    .setValue(m.cropStart,       juce::dontSendNotification);
    endSlider      .setValue(m.cropEnd,         juce::dontSendNotification);
    loopStartSlider.setValue(m.loopStart,       juce::dontSendNotification);
    loopEndSlider  .setValue(m.loopEnd,         juce::dontSendNotification);
    smoothSlider   .setValue(m.loopCrossfadeMs, juce::dontSendNotification);
    autoLoopBtn    .setToggleState(m.autoLoop,      juce::dontSendNotification);
    oneShotBtn     .setToggleState(m.oneShotMode,   juce::dontSendNotification);
    playAcrossBtn  .setToggleState(m.pitchTracking, juce::dontSendNotification);
    rootNoteBox    .setSelectedId(m.rootMidi + 1,   juce::dontSendNotification);
    for (int i = 0; i < categoryBox.getNumItems(); ++i)
        if (categoryBox.getItemText(i) == m.category)
            categoryBox.setSelectedItemIndex(i, juce::dontSendNotification);
}

void AudioCropPanel::pullMetaFromUi(CropMeta& m)
{
    m.cropStart       = startSlider.getValue();
    m.cropEnd         = endSlider.getValue();
    m.loopStart       = loopStartSlider.getValue();
    m.loopEnd         = loopEndSlider.getValue();
    m.loopCrossfadeMs = smoothSlider.getValue();
    m.autoLoop        = autoLoopBtn.getToggleState();
    m.oneShotMode     = oneShotBtn.getToggleState();
    m.pitchTracking   = playAcrossBtn.getToggleState();
    m.loopEnabled     = m.autoLoop && ! m.oneShotMode;
}

// Write a trimmed WAV containing only the [cropStart..cropEnd] portion of
// `src` to `dest`. Returns true on success.
static bool writeTrimmedWav(const juce::File& src, const juce::File& dest,
                            double cropStart, double cropEnd)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(src));
    if (! reader) return false;

    const int64_t total = reader->lengthInSamples;
    if (total <= 0) return false;
    const int64_t s0  = (int64_t) (juce::jlimit(0.0, 1.0, cropStart) * (double) total);
    const int64_t s1  = (int64_t) (juce::jlimit(0.0, 1.0, cropEnd)   * (double) total);
    const int64_t len = juce::jmax<int64_t>(0, s1 - s0);
    if (len <= 0) return false;

    const int numCh = (int) reader->numChannels;
    juce::AudioBuffer<float> buf(numCh, (int) len);
    reader->read(&buf, 0, (int) len, s0, true, numCh > 1);

    dest.deleteFile();
    auto out = std::unique_ptr<juce::FileOutputStream>(dest.createOutputStream());
    if (! out) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(out.get(), reader->sampleRate, (unsigned) numCh,
                            juce::jmax(16, (int) reader->bitsPerSample), {}, 0));
    if (! writer) return false;
    out.release(); // writer owns the stream now
    writer->writeFromAudioSampleBuffer(buf, 0, (int) len);
    return true;
}

void AudioCropPanel::persistSelected()
{
    if (selectedIndex < 0) return;
    auto m = samples[(size_t) selectedIndex];
    pullMetaFromUi(m);
    m.needsReview = false;

    auto src = juce::File(m.sourcePath);
    if (! src.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Save crop failed", "Source file no longer exists:\n" + src.getFullPathName());
        return;
    }

    // Overwrite the original with the cropped audio. No backup, no new versions.
    if (! writeTrimmedWav(src, src, m.cropStart, m.cropEnd))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Save crop failed", "Could not write trimmed audio to:\n" + src.getFullPathName());
        return;
    }

    // After writing the trimmed file, the crop now covers the whole file.
    m.cropStart         = 0.0;
    m.cropEnd           = 1.0;

    samples[(size_t) selectedIndex] = m;
    if (waveform) waveform->loadFor(src);
    pushUiFromMeta(m);
    writeMeta(m);
    sampleList.repaint();
    if (waveform) waveform->repaint();

    // Force the audio engine to drop cached buffers so playback picks up the
    // new file, and tell the editor to refresh the preset browser.
    dida::SampleLibrary::invalidateCache();
    if (onLibraryChanged) onLibraryChanged();
}

void AudioCropPanel::resetSelectedToOriginal()
{
    if (selectedIndex < 0) return;
    auto& m = samples[(size_t) selectedIndex];
    auto src = juce::File(m.sourcePath);
    auto backup = src.getSiblingFile(src.getFileNameWithoutExtension() + ".original" + src.getFileExtension());
    if (backup.existsAsFile())
    {
        src.deleteFile();
        backup.copyFileTo(src);
        dida::SampleLibrary::invalidateCache();
        if (onLibraryChanged) onLibraryChanged();
        m.cropStart = 0.0; m.cropEnd = 1.0;
        m.loopStart = 0.2; m.loopEnd = 0.95;
        m.loopCrossfadeMs = 15.0;
        pushUiFromMeta(m);
        if (waveform) waveform->loadFor(src);
        if (waveform) waveform->repaint();
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
            "Reset To Original", "No original backup exists for this file.\n"
            "The crop tab now overwrites the original file when saving.");
    }
}

void AudioCropPanel::startPreview()
{
    if (selectedIndex < 0) return;
    stopPreview();
    if (! deviceStarted)
    {
        deviceManager.initialiseWithDefaultDevices(0, 2);
        deviceManager.addAudioCallback(&audioPlayer);
        deviceStarted = true;
    }
    auto& m = samples[(size_t) selectedIndex];
    auto file = juce::File(m.sourcePath);
    if (auto* reader = formatManager.createReaderFor(file))
    {
        readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
        readerSource->setLooping(m.autoLoop && ! m.oneShotMode);
        transport = std::make_unique<juce::AudioTransportSource>();
        transport->setSource(readerSource.get(), 0, nullptr, reader->sampleRate);
        const double total = reader->lengthInSamples / reader->sampleRate;
        transport->setPosition(m.cropStart * total);
        audioPlayer.setSource(transport.get());
        transport->start();
        startTimer(50);
    }
}

void AudioCropPanel::stopPreview()
{
    stopTimer();
    if (transport) { transport->stop(); transport->setSource(nullptr); }
    audioPlayer.setSource(nullptr);
    transport.reset();
    readerSource.reset();
}

void AudioCropPanel::timerCallback()
{
    if (selectedIndex < 0 || ! transport) { stopPreview(); return; }
    auto& m = samples[(size_t) selectedIndex];
    const double total = transport->getLengthInSeconds();
    if (total <= 0.0) return;
    const double pos = transport->getCurrentPosition();
    if (pos >= m.cropEnd * total)
    {
        if (m.autoLoop && ! m.oneShotMode)
            transport->setPosition(m.loopStart * total);
        else
            stopPreview();
    }
}

// =============================================================================
// Layout
// =============================================================================
void AudioCropPanel::paint(juce::Graphics& g)
{
    auto& C = Theme::getColors();
    g.fillAll(C.background);
    g.setColour(C.surface);
    auto right = getLocalBounds().removeFromRight(280);
    g.fillRect(right);
    auto left = getLocalBounds().removeFromLeft(280);
    g.fillRect(left);
}

void AudioCropPanel::resized()
{
    auto r = getLocalBounds();
    auto leftCol  = r.removeFromLeft(280).reduced(8);
    auto rightCol = r.removeFromRight(280).reduced(8);
    auto center   = r.reduced(8);

    // Left
    categoryFilter.setBounds(leftCol.removeFromTop(26));
    leftCol.removeFromTop(4);
    searchBox.setBounds(leftCol.removeFromTop(26));
    leftCol.removeFromTop(6);
    auto leftBottom = leftCol.removeFromBottom(96);
    saveAllButton .setBounds(leftBottom.removeFromBottom(28));
    leftBottom.removeFromBottom(4);
    rescanButton  .setBounds(leftBottom.removeFromBottom(28));
    leftBottom.removeFromBottom(4);
    importButton  .setBounds(leftBottom.removeFromBottom(28));
    sampleList.setBounds(leftCol);

    // Center
    auto wfTopRow = center.removeFromTop(24);
    zoomOut  .setBounds(wfTopRow.removeFromLeft(28));
    zoomIn   .setBounds(wfTopRow.removeFromLeft(28));
    resetView.setBounds(wfTopRow.removeFromLeft(90));
    snapZero .setBounds(wfTopRow.removeFromLeft(180));
    waveform->setBounds(center);

    // Right
    auto row = [&rightCol](int h) { auto rr = rightCol.removeFromTop(h); rightCol.removeFromTop(4); return rr; };
    auto labeled = [&](juce::Label& l, juce::Slider& s) {
        auto rr = row(36);
        l.setBounds(rr.removeFromTop(14));
        s.setBounds(rr);
    };
    labeled(startLabel,     startSlider);
    labeled(endLabel,       endSlider);
    labeled(loopStartLabel, loopStartSlider);
    labeled(loopEndLabel,   loopEndSlider);
    labeled(smoothLabel,    smoothSlider);
    autoLoopBtn   .setBounds(row(22));
    oneShotBtn    .setBounds(row(22));
    playAcrossBtn .setBounds(row(22));
    row(4);
    rootNoteBox   .setBounds(row(24));
    categoryBox   .setBounds(row(24));
    row(6);
    previewBtn    .setBounds(row(28));
    saveBtn       .setBounds(row(28));
    resetBtn      .setBounds(row(28));
}
