#include "PluginEditor.h"

DiditagainEditor::DiditagainEditor(DiditagainProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(1200, 760);
    setResizable(true, true);
    setResizeLimits(960, 640, 1920, 1200);

    // --- Build content panels ---
    layerPanel = std::make_unique<LayerEditor>(processor.getAPVTS());
    addAndMakeVisible(*layerPanel);

    macroPanel = std::make_unique<MacroPanel>(processor.getAPVTS());
    addAndMakeVisible(*macroPanel); // macros stay visible across tabs

    synthPanel = std::make_unique<MainSynthPanel>(processor.getAPVTS());
    addChildComponent(*synthPanel);

    modPanel = std::make_unique<ModPanel>(processor.getAPVTS());
    addChildComponent(*modPanel);

    fxPanel = std::make_unique<FxPanel>(processor.getAPVTS());
    addChildComponent(*fxPanel);

    importPanel = std::make_unique<ImportReviewPanel>();
    importPanel->onRescan = [this]() {
        processor.getPresetManager().scanPresetDirectory();
        refreshPresetCombo();
    };
    importPanel->onOpenInbox = [this]() {
        auto inbox = processor.getPresetManager().getUserPresetDirectory().getChildFile("_inbox");
        inbox.createDirectory();
        inbox.revealToUser();
    };
    addChildComponent(*importPanel);

    settingsPanel = std::make_unique<SettingsPanel>(processor.getAPVTS());
    addChildComponent(*settingsPanel);

    accountPanel = std::make_unique<AccountPanel>();
    addChildComponent(*accountPanel);

    presetBrowserPanel = std::make_unique<PresetBrowser>();
    addChildComponent(*presetBrowserPanel);

    {
        auto& pm = processor.getPresetManager();
        juce::StringArray names, cats;
        for (int i = 0; i < pm.getNumPresets(); ++i)
        {
            names.add(pm.getPresetName(i));
            cats.add("All"); // category info not exposed via API; show all
        }
        presetBrowserPanel->setPresets(names, cats);
        presetBrowserPanel->onPresetSelected = [this](int idx) {
            processor.getPresetManager().loadPreset(idx);
            presetSelector.setSelectedId(idx + 1, juce::dontSendNotification);
        };
    }

    setupTabs();
    switchTab(Tab::Layers);

    // setSize() fires resized() before the child controls above exist. Some
    // hosts (including FL Studio) won't immediately send another resize event,
    // which leaves every child component at 0x0 and makes the editor look blank.
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

void DiditagainEditor::setupTabs()
{
    auto setupTab = [this](juce::TextButton& btn, Tab tab) {
        btn.onClick = [this, tab]() { switchTab(tab); };
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e1e2e));
        btn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffe0e0e0));
        addAndMakeVisible(btn);
    };

    setupTab(tabBrowser, Tab::Browser);
    setupTab(tabLayers, Tab::Layers);
    setupTab(tabSynth, Tab::Synth);
    setupTab(tabMod, Tab::Mod);
    setupTab(tabFX, Tab::FX);
    setupTab(tabImport, Tab::Import);
    setupTab(tabSettings, Tab::Settings);
    setupTab(tabAccount, Tab::Account);

    modeToggle.setClickingTogglesState(true);
    modeToggle.setToggleState(advancedMode, juce::dontSendNotification);
    modeToggle.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff1e1e2e));
    modeToggle.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff8b5cf6));
    modeToggle.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
    modeToggle.setColour(juce::TextButton::textColourOffId,  juce::Colour(0xffe0e0e0));
    modeToggle.onClick = [this]() {
        advancedMode = modeToggle.getToggleState();
        modeToggle.setButtonText(advancedMode ? "ADVANCED" : "SIMPLE");
        switchTab(currentTab);
        resized();
        repaint();
    };
    addAndMakeVisible(modeToggle);

    addAndMakeVisible(presetSelector);
    addAndMakeVisible(prevPreset);
    addAndMakeVisible(nextPreset);
    addAndMakeVisible(savePreset);
    addAndMakeVisible(cycleTestButton);
    addAndMakeVisible(cycleTestLog);

    cycleTestLog.setMultiLine(true);
    cycleTestLog.setReadOnly(true);
    cycleTestLog.setScrollbarsShown(true);
    cycleTestLog.setCaretVisible(false);
    cycleTestLog.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0F1118));
    cycleTestLog.setColour(juce::TextEditor::textColourId,       juce::Colour(0xffB8C0D0));
    cycleTestLog.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    cycleTestLog.setVisible(false);

    cycleTester = std::make_unique<PresetCycleTester>(processor);
    cycleTester->onLog = [this](const juce::String& line)
    {
        juce::MessageManager::callAsync([this, line]
        {
            cycleTestLog.moveCaretToEnd();
            cycleTestLog.insertTextAtCaret(line + "\n");
        });
    };
    cycleTester->onFinished = [this](int p, int f, int total)
    {
        juce::MessageManager::callAsync([this, p, f, total]
        {
            cycleTestButton.setButtonText("CYCLE TEST");
            const auto summary = juce::String("Done: ") + juce::String(p) + "/"
                + juce::String(total) + " passed, " + juce::String(f) + " mismatches";
            cycleTestLog.moveCaretToEnd();
            cycleTestLog.insertTextAtCaret(summary + "\n");
        });
    };

    cycleTestButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff8b5cf6));
    cycleTestButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    cycleTestButton.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
    cycleTestButton.onClick = [this]()
    {
        if (! cycleTester) return;
        if (cycleTester->isRunning())
        {
            cycleTester->stop();
            cycleTestButton.setButtonText("CYCLE TEST");
            return;
        }
        cycleTestLog.clear();
        cycleTestLog.setVisible(true);
        cycleTestButton.setButtonText("STOP TEST");
        cycleTester->start(/*passes*/ 2, /*intervalMs*/ 350, /*maxWaitMs*/ 1500);
        resized();
    };

    refreshPresetCombo();

    presetSelector.onChange = [this]() {
        const int idx = presetSelector.getSelectedId() - 1;
        if (idx >= 0)
            processor.getPresetManager().loadPreset(idx);
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

void DiditagainEditor::switchTab(Tab tab)
{
    currentTab = tab;
    if (layerPanel)         layerPanel->setVisible(tab == Tab::Layers);
    if (synthPanel)         synthPanel->setVisible(tab == Tab::Synth);
    if (presetBrowserPanel) presetBrowserPanel->setVisible(tab == Tab::Browser);
    if (modPanel)           modPanel->setVisible(tab == Tab::Mod);
    if (fxPanel)            fxPanel->setVisible(tab == Tab::FX);
    if (importPanel)        importPanel->setVisible(tab == Tab::Import);
    if (settingsPanel)      settingsPanel->setVisible(tab == Tab::Settings);
    if (accountPanel)       accountPanel->setVisible(tab == Tab::Account);

    const bool deep = advancedMode;
    tabSynth.setEnabled(deep);
    tabMod.setEnabled(deep);
    tabFX.setEnabled(deep);
    tabSynth.setAlpha(deep ? 1.0f : 0.4f);
    tabMod.setAlpha(deep ? 1.0f : 0.4f);
    tabFX.setAlpha(deep ? 1.0f : 0.4f);

    repaint();
}

void DiditagainEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0B0D10));

    g.setColour(juce::Colour(0xff151922));
    g.fillRect(0, 0, getWidth(), 50);
    g.setColour(juce::Colour(0xff8b5cf6));
    g.setFont(juce::Font(18.0f).boldened());
    g.drawText("DIDITAGAIN STUDIO", 15, 0, 240, 50, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff8a93a6));
    g.setFont(juce::Font(11.0f));
    g.drawText("Hybrid 4-Layer Workstation v2", 15, 28, 260, 16, juce::Justification::centredLeft);

    auto tabBounds = [this](Tab t) -> juce::Rectangle<int> {
        int idx = static_cast<int>(t);
        return { 260 + idx * 78, 12, 72, 26 };
    };
    for (int i = 0; i < 8; ++i)
    {
        Tab t = static_cast<Tab>(i);
        if (t == currentTab)
        {
            g.setColour(juce::Colour(0xff8b5cf6).withAlpha(0.22f));
            g.fillRoundedRectangle(tabBounds(t).toFloat(), 4.0f);
        }
    }

    g.setColour(juce::Colour(0xff0F1118));
    g.fillRect(0, 50, getWidth(), 40);

    if (macroPanel)
    {
        g.setColour(juce::Colour(0xff10131a));
        g.fillRect(macroPanel->getBounds().expanded(0, 4));
    }
}

void DiditagainEditor::resized()
{
    int pw = getWidth();
    prevPreset.setBounds(pw - 350, 56, 30, 28);
    presetSelector.setBounds(pw - 315, 56, 200, 28);
    nextPreset.setBounds(pw - 110, 56, 30, 28);
    savePreset.setBounds(pw - 75, 56, 60, 28);

    cycleTestButton.setBounds(pw - 470, 56, 110, 28);
    modeToggle.setBounds(pw - 590, 56, 110, 28);

    tabBrowser .setBounds(260 + 0 * 78, 12, 72, 26);
    tabLayers  .setBounds(260 + 1 * 78, 12, 72, 26);
    tabSynth   .setBounds(260 + 2 * 78, 12, 72, 26);
    tabMod     .setBounds(260 + 3 * 78, 12, 72, 26);
    tabFX      .setBounds(260 + 4 * 78, 12, 72, 26);
    tabImport  .setBounds(260 + 5 * 78, 12, 72, 26);
    tabSettings.setBounds(260 + 6 * 78, 12, 72, 26);
    tabAccount .setBounds(260 + 7 * 78, 12, 72, 26);

    const int macroH = 110;
    auto full = juce::Rectangle<int>(8, 96, getWidth() - 16, getHeight() - 104);
    auto macroArea = full.removeFromBottom(macroH);
    if (macroPanel) macroPanel->setBounds(macroArea);

    auto content = full.withTrimmedBottom(6);

    if (cycleTestLog.isVisible())
    {
        const int logH = juce::jmin(180, content.getHeight() / 3);
        cycleTestLog.setBounds(content.removeFromBottom(logH).reduced(2));
    }

    if (layerPanel)         layerPanel->setBounds(content);
    if (synthPanel)         synthPanel->setBounds(content);
    if (presetBrowserPanel) presetBrowserPanel->setBounds(content);
    if (modPanel)           modPanel->setBounds(content);
    if (fxPanel)            fxPanel->setBounds(content);
    if (importPanel)        importPanel->setBounds(content);
    if (settingsPanel)      settingsPanel->setBounds(content);
    if (accountPanel)       accountPanel->setBounds(content);
}
