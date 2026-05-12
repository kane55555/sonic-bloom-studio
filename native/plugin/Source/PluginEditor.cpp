#include "PluginEditor.h"
#include "UI/Theme.h"

DiditagainEditor::DiditagainEditor(DiditagainProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(1200, 760);
    setResizable(true, true);
    setResizeLimits(960, 640, 1920, 1200);

    layerPanel = std::make_unique<LayerEditor>(processor.getAPVTS());
    addAndMakeVisible(*layerPanel);

    macroPanel = std::make_unique<MacroPanel>(processor.getAPVTS());
    addAndMakeVisible(*macroPanel);

    synthPanel    = std::make_unique<MainSynthPanel>(processor.getAPVTS());
    modPanel      = std::make_unique<ModPanel>(processor.getAPVTS());
    fxPanel       = std::make_unique<FxPanel>(processor.getAPVTS());
    importPanel   = std::make_unique<ImportReviewPanel>();
    settingsPanel = std::make_unique<SettingsPanel>(processor.getAPVTS());
    accountPanel  = std::make_unique<AccountPanel>();

    importPanel->presetManager = &processor.getPresetManager();
    importPanel->onRescan = [this]() {
        processor.getPresetManager().scanPresetDirectory();
        refreshBrowserPresets();
    };
    importPanel->onPresetsCreated = [this]() {
        processor.getPresetManager().scanPresetDirectory();
        refreshBrowserPresets();
    };
    importPanel->onOpenInbox = []() {
        auto samples = dida::SampleLibrary::getSamplesRoot().getChildFile("Imported");
        samples.createDirectory();
        samples.revealToUser();
    };

    addChildComponent(*synthPanel);
    addChildComponent(*modPanel);
    addChildComponent(*fxPanel);
    addChildComponent(*importPanel);
    addChildComponent(*settingsPanel);
    addChildComponent(*accountPanel);

    presetBrowserPanel = std::make_unique<PresetBrowser>();
    addAndMakeVisible(*presetBrowserPanel);

    audioCropPanel = std::make_unique<AudioCropPanel>();
    addChildComponent(*audioCropPanel);

    presetBrowserPanel->onPresetSelected = [this](int idx) {
        processor.getPresetManager().loadPreset(idx);
        presetSelector.setSelectedId(idx + 1, juce::dontSendNotification);
    };
    refreshBrowserPresets();

    overlayClose.setColour(juce::TextButton::buttonColourId, Theme::getColors().surfaceElevated);
    overlayClose.setColour(juce::TextButton::textColourOffId, Theme::getColors().textPrimary);
    overlayClose.onClick = [this]() { hideOverlay(); };
    overlayTitle.setFont(Theme::getHeadingFont(16.0f));
    overlayTitle.setColour(juce::Label::textColourId, Theme::getColors().accentTeal);
    overlayClose.setVisible(false);
    overlayTitle.setVisible(false);
    addChildComponent(overlayClose);
    addChildComponent(overlayTitle);

    setupTabs();
    switchTab(Tab::Browser);

    resized();
    repaint();
}

DiditagainEditor::~DiditagainEditor() {}

void DiditagainEditor::refreshPresetCombo()
{
    presetSelector.clear(juce::dontSendNotification);
    auto& pm = processor.getPresetManager();
    for (int i = 0; i < pm.getNumPresets(); ++i)
        presetSelector.addItem(pm.getPresetName(i), i + 1);
    presetSelector.setSelectedId(pm.getCurrentPresetIndex() + 1, juce::dontSendNotification);
}

void DiditagainEditor::refreshBrowserPresets()
{
    if (! presetBrowserPanel) return;
    auto& pm = processor.getPresetManager();
    const int n = pm.getNumPresets();

    // If any user (imported) presets exist, hide the factory placeholders so
    // the browser shows the producer's own one-shots only. Otherwise fall back
    // to showing the factory list so the browser is never empty.
    bool hasUser = false;
    for (int i = 0; i < n; ++i) if (! pm.getPresetIsFactory(i)) { hasUser = true; break; }

    juce::StringArray names, cats;
    for (int i = 0; i < n; ++i)
    {
        if (hasUser && pm.getPresetIsFactory(i)) continue;
        names.add(pm.getPresetName(i));
        auto cat = pm.getPresetCategory(i);
        cats.add(cat.isNotEmpty() ? cat : juce::String("Other"));
    }
    presetBrowserPanel->setPresets(names, cats);
}

void DiditagainEditor::setupTabs()
{
    auto& C = Theme::getColors();
    auto setupTab = [this, &C](juce::TextButton& btn, Tab tab) {
        btn.onClick = [this, tab]() { switchTab(tab); };
        btn.setColour(juce::TextButton::buttonColourId, C.surface);
        btn.setColour(juce::TextButton::textColourOnId, C.accentTeal);
        btn.setColour(juce::TextButton::textColourOffId, C.textSecondary);
        addAndMakeVisible(btn);
    };
    setupTab(tabBrowser,   Tab::Browser);
    setupTab(tabLayers,    Tab::Layers);
    setupTab(tabFX,        Tab::FX);
    setupTab(tabAudioCrop, Tab::AudioCrop);

    menuButton.setColour(juce::TextButton::buttonColourId, C.surface);
    menuButton.setColour(juce::TextButton::textColourOffId, C.accentTeal);
    menuButton.onClick = [this]() { openMenu(); };
    addAndMakeVisible(menuButton);

    addAndMakeVisible(presetSelector);
    addAndMakeVisible(prevPreset);
    addAndMakeVisible(nextPreset);
    addAndMakeVisible(savePreset);

    directMonitorButton.setClickingTogglesState(true);
    directMonitorButton.setTooltip("Bypass reverb + delay for low-latency tracking. Turn off for mixdown.");
    directMonitorButton.setColour(juce::TextButton::buttonColourId, C.surface);
    directMonitorButton.setColour(juce::TextButton::buttonOnColourId, C.accentTeal);
    directMonitorButton.setColour(juce::TextButton::textColourOffId, C.accentTeal);
    directMonitorButton.setColour(juce::TextButton::textColourOnId, C.surface);
    addAndMakeVisible(directMonitorButton);
    directMonitorAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getAPVTS(), "directMonitor", directMonitorButton);

    refreshPresetCombo();

    presetSelector.onChange = [this]() {
        const int idx = presetSelector.getSelectedId() - 1;
        if (idx >= 0) processor.getPresetManager().loadPreset(idx);
    };
    prevPreset.onClick = [this]() {
        auto& pm2 = processor.getPresetManager();
        int idx = pm2.getCurrentPresetIndex();
        if (idx > 0) { pm2.loadPreset(idx - 1); presetSelector.setSelectedId(idx, juce::dontSendNotification); }
    };
    nextPreset.onClick = [this]() {
        auto& pm2 = processor.getPresetManager();
        int idx = pm2.getCurrentPresetIndex();
        if (idx < pm2.getNumPresets() - 1) { pm2.loadPreset(idx + 1); presetSelector.setSelectedId(idx + 2, juce::dontSendNotification); }
    };
}

void DiditagainEditor::openMenu()
{
    juce::PopupMenu m;
    m.addItem(1, "Import One-Shot...");
    m.addSeparator();
    m.addItem(2, "Advanced Sound Design");
    m.addItem(3, "Modulation");
    m.addItem(4, "MIDI / Performance");
    m.addItem(5, "Library / Settings");
    m.addItem(6, "Account");
    m.addSeparator();
    m.addItem(7, "About DIDITAGAIN STUDIO", false);

    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menuButton),
        [this](int result)
        {
            switch (result)
            {
                case 1: showOverlay(importPanel.get(),   "IMPORT ONE-SHOT"); break;
                case 2: showOverlay(synthPanel.get(),    "ADVANCED SOUND DESIGN"); break;
                case 3: showOverlay(modPanel.get(),      "MODULATION"); break;
                case 4: showOverlay(settingsPanel.get(), "MIDI / PERFORMANCE"); break;
                case 5: showOverlay(settingsPanel.get(), "LIBRARY / SETTINGS"); break;
                case 6: showOverlay(accountPanel.get(),  "ACCOUNT"); break;
                default: break;
            }
        });
}

void DiditagainEditor::showOverlay(juce::Component* panel, const juce::String& title)
{
    if (panel == nullptr) return;
    overlayPanel = panel;
    overlayTitle.setText(title, juce::dontSendNotification);
    panel->setVisible(true);
    panel->toFront(false);
    overlayTitle.setVisible(true);
    overlayClose.setVisible(true);
    overlayTitle.toFront(false);
    overlayClose.toFront(false);
    resized();
    repaint();
}

void DiditagainEditor::hideOverlay()
{
    if (overlayPanel) overlayPanel->setVisible(false);
    overlayPanel = nullptr;
    overlayTitle.setVisible(false);
    overlayClose.setVisible(false);
    repaint();
}

void DiditagainEditor::switchTab(Tab tab)
{
    currentTab = tab;
    if (layerPanel)         layerPanel->setVisible(tab == Tab::Layers);
    if (presetBrowserPanel) presetBrowserPanel->setVisible(tab == Tab::Browser);
    if (fxPanel)            fxPanel->setVisible(tab == Tab::FX && overlayPanel == nullptr);
    if (audioCropPanel)     audioCropPanel->setVisible(tab == Tab::AudioCrop);
    repaint();
}

void DiditagainEditor::paint(juce::Graphics& g)
{
    auto& C = Theme::getColors();
    g.fillAll(C.background);

    // Header band
    g.setColour(C.surface);
    g.fillRect(0, 0, getWidth(), 56);

    // Logo placeholder slot — replace by drawing src/assets/diditagain-logo.* via BinaryData
    g.setColour(C.accentTeal);
    g.setFont(Theme::getHeadingFont(20.0f));
    g.drawText("DIDITAGAIN", 16, 6, 220, 28, juce::Justification::centredLeft);
    g.setColour(C.textSecondary);
    g.setFont(Theme::getBodyFont(10.0f));
    g.drawText("STUDIO  /  Main Logo Asset Here", 16, 32, 280, 16, juce::Justification::centredLeft);

    // Active tab underline (teal)
    auto tabUnder = [&](juce::TextButton& b, bool active) {
        if (! active) return;
        g.setColour(C.accentTeal);
        auto bb = b.getBounds();
        g.fillRect(bb.getX(), bb.getBottom() + 2, bb.getWidth(), 2);
    };
    tabUnder(tabBrowser, currentTab == Tab::Browser);
    tabUnder(tabLayers,  currentTab == Tab::Layers);
    tabUnder(tabFX,        currentTab == Tab::FX);
    tabUnder(tabAudioCrop, currentTab == Tab::AudioCrop);

    // Macro band background
    if (macroPanel)
    {
        g.setColour(C.surface);
        g.fillRect(macroPanel->getBounds().expanded(0, 4));
    }

    // Modal overlay scrim
    if (overlayPanel != nullptr)
    {
        g.setColour(juce::Colour(0xCC000000));
        g.fillRect(0, 56, getWidth(), getHeight() - 56);
        g.setColour(C.surface);
        g.fillRoundedRectangle(overlayPanel->getBounds().expanded(8).toFloat(), 8.0f);
        g.setColour(C.accentTeal);
        g.drawRoundedRectangle(overlayPanel->getBounds().expanded(8).toFloat(), 8.0f, 1.0f);
    }
}

void DiditagainEditor::resized()
{
    int pw = getWidth();

    // Header right-cluster
    menuButton         .setBounds(pw - 50,  14, 36, 28);
    savePreset         .setBounds(pw - 110, 14, 56, 28);
    nextPreset         .setBounds(pw - 145, 14, 30, 28);
    presetSelector     .setBounds(pw - 350, 14, 200, 28);
    prevPreset         .setBounds(pw - 385, 14, 30, 28);
    directMonitorButton.setBounds(pw - 460, 14, 68, 28);

    // Tabs (centered-ish, left of preset cluster)
    int tabsLeft = 320;
    tabBrowser  .setBounds(tabsLeft + 0 * 90, 18, 80, 26);
    tabLayers   .setBounds(tabsLeft + 1 * 90, 18, 80, 26);
    tabFX       .setBounds(tabsLeft + 2 * 90, 18, 80, 26);
    tabAudioCrop.setBounds(tabsLeft + 3 * 90, 18, 100, 26);

    const int macroH = 110;
    auto full = juce::Rectangle<int>(8, 64, getWidth() - 16, getHeight() - 72);
    auto macroArea = full.removeFromBottom(macroH);
    if (macroPanel) macroPanel->setBounds(macroArea);

    auto content = full.withTrimmedBottom(6);

    if (layerPanel)         layerPanel->setBounds(content);
    if (presetBrowserPanel) presetBrowserPanel->setBounds(content);
    if (fxPanel)            fxPanel->setBounds(content);
    if (audioCropPanel)     audioCropPanel->setBounds(content);

    // Modal overlay region (smaller, centered)
    auto modal = getLocalBounds().reduced(80, 90).withTrimmedTop(10);
    overlayTitle.setBounds(modal.getX() + 8, modal.getY() - 24, modal.getWidth() - 100, 22);
    overlayClose.setBounds(modal.getRight() - 84, modal.getY() - 28, 80, 24);
    if (overlayPanel) overlayPanel->setBounds(modal);
}
