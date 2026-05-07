#include "PluginEditor.h"

DiditagainEditor::DiditagainEditor(DiditagainProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(1200, 760);
    setResizable(true, true);
    setResizeLimits(960, 640, 1920, 1200);

    // --- Build content panels ---
    synthPanel = std::make_unique<MainSynthPanel>(processor.getAPVTS());
    addAndMakeVisible(*synthPanel);

    modPanel = std::make_unique<ModPanel>(processor.getAPVTS());
    addChildComponent(*modPanel);

    fxPanel = std::make_unique<FxPanel>(processor.getAPVTS());
    addChildComponent(*fxPanel);

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
    switchTab(Tab::Synth);

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
    setupTab(tabSynth, Tab::Synth);
    setupTab(tabMod, Tab::Mod);
    setupTab(tabFX, Tab::FX);
    setupTab(tabSettings, Tab::Settings);
    setupTab(tabAccount, Tab::Account);

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
    if (synthPanel)         synthPanel->setVisible(tab == Tab::Synth);
    if (presetBrowserPanel) presetBrowserPanel->setVisible(tab == Tab::Browser);
    if (modPanel)           modPanel->setVisible(tab == Tab::Mod);
    if (fxPanel)            fxPanel->setVisible(tab == Tab::FX);
    if (settingsPanel)      settingsPanel->setVisible(tab == Tab::Settings);
    if (accountPanel)       accountPanel->setVisible(tab == Tab::Account);
    repaint();
}

void DiditagainEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0B0D10));

    // Header
    g.setColour(juce::Colour(0xff151922));
    g.fillRect(0, 0, getWidth(), 50);
    g.setColour(juce::Colour(0xff8b5cf6));
    g.setFont(juce::Font(18.0f).boldened());
    g.drawText("DIDITAGAIN STUDIO", 15, 0, 250, 50, juce::Justification::centredLeft);

    // Active tab pill
    auto tabBounds = [this](Tab t) -> juce::Rectangle<int> {
        int idx = static_cast<int>(t);
        return { 280 + idx * 90, 12, 82, 26 };
    };
    for (int i = 0; i < 6; ++i)
    {
        Tab t = static_cast<Tab>(i);
        if (t == currentTab)
        {
            g.setColour(juce::Colour(0xff8b5cf6).withAlpha(0.22f));
            g.fillRoundedRectangle(tabBounds(t).toFloat(), 4.0f);
        }
    }

    // Sub-bar background
    g.setColour(juce::Colour(0xff0F1118));
    g.fillRect(0, 50, getWidth(), 40);
}

void DiditagainEditor::resized()
{
    int pw = getWidth();
    prevPreset.setBounds(pw - 350, 56, 30, 28);
    presetSelector.setBounds(pw - 315, 56, 200, 28);
    nextPreset.setBounds(pw - 110, 56, 30, 28);
    savePreset.setBounds(pw - 75, 56, 60, 28);

    cycleTestButton.setBounds(pw - 470, 56, 110, 28);

    tabBrowser.setBounds(280, 12, 82, 26);
    tabSynth.setBounds(370, 12, 82, 26);
    tabMod.setBounds(460, 12, 82, 26);
    tabFX.setBounds(550, 12, 82, 26);
    tabSettings.setBounds(640, 12, 82, 26);
    tabAccount.setBounds(730, 12, 82, 26);

    auto content = juce::Rectangle<int>(8, 96, getWidth() - 16, getHeight() - 104);

    if (cycleTestLog.isVisible())
    {
        const int logH = juce::jmin(180, content.getHeight() / 3);
        cycleTestLog.setBounds(content.removeFromBottom(logH).reduced(2));
    }

    if (synthPanel)         synthPanel->setBounds(content);
    if (presetBrowserPanel) presetBrowserPanel->setBounds(content);
    if (modPanel)           modPanel->setBounds(content);
    if (fxPanel)            fxPanel->setBounds(content);
    if (settingsPanel)      settingsPanel->setBounds(content);
    if (accountPanel)       accountPanel->setBounds(content);
}
