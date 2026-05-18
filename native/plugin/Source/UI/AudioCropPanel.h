#pragma once
#include <JuceHeader.h>

// Permanent "Audio Crop" tab. Lists every imported user sample under
// <Documents>/DIDITAGAIN STUDIO/Samples/Imported, User, and Presets/User, shows a
// waveform with crop/loop markers and producer-friendly controls, and
// stores per-sample crop metadata as JSON next to the audio file.
//
// Metadata is non-destructive: the original WAV is never overwritten
// unless the user picks "Save As New Version".
class AudioCropPanel : public juce::Component,
                       private juce::Timer
{
public:
    AudioCropPanel();
    ~AudioCropPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Re-scan all imported sample folders and rebuild the list.
    void rescan();

    // Fired whenever the on-disk sample library changes (rescan / save crop).
    // Editor uses this to rescan presets and refresh the browser tab.
    std::function<void()> onLibraryChanged;

private:
    struct CropMeta
    {
        juce::String sampleId;
        juce::String sourcePath;            // absolute path on disk
        juce::String originalFileName;
        juce::String category    = "Uncategorized";
        juce::String rootNote    = "C5";
        int    rootMidi          = 72;
        double cropStart         = 0.0;     // 0..1
        double cropEnd           = 1.0;
        bool   loopEnabled       = true;
        double loopStart         = 0.2;
        double loopEnd           = 0.95;
        double loopCrossfadeMs   = 15.0;
        bool   autoLoop          = true;
        bool   oneShotMode       = false;
        bool   pitchTracking     = true;
        bool   needsReview       = false;
        juce::String dateModified;
    };

    // ---- data ----
    std::vector<CropMeta> samples;
    int  selectedIndex = -1;
    juce::String filterCategory = "All Imported Samples";
    juce::String searchText;

    // ---- left column ----
    juce::ComboBox  categoryFilter;
    juce::TextEditor searchBox;
    juce::ListBox   sampleList;
    juce::TextButton importButton  { "Import New Sample" };
    juce::TextButton rescanButton  { "Rescan Imported Samples" };
    juce::TextButton saveAllButton { "Save All Edited Samples" };

    // ---- center waveform ----
    class Waveform : public juce::Component
    {
    public:
        Waveform(AudioCropPanel& o) : owner(o) {}
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
        void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;
        void loadFor(const juce::File& f);

        // Zoom controls
        void zoomBy(double factor, double anchorFrac);
        void resetZoom();
        void panBy(double deltaFrac);

        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;

        // View window in [0..1] of buffer
        double viewStart = 0.0;
        double viewSpan  = 1.0; // 1.0 = full, smaller = zoomed in

    private:
        AudioCropPanel& owner;
        int dragHandle = -1; // 0..3 = cropStart, cropEnd, loopStart, loopEnd
        bool panning = false;
        int  panLastX = 0;

        double screenToFrac(int x) const;
    };
    std::unique_ptr<Waveform> waveform;
    juce::TextButton zoomIn   { "+" };
    juce::TextButton zoomOut  { "-" };
    juce::TextButton resetView{ "Reset View" };
    juce::ToggleButton snapZero{ "Snap to zero-crossing" };

    // ---- right controls ----
    juce::Slider startSlider, endSlider, loopStartSlider, loopEndSlider, smoothSlider;
    juce::Label  startLabel, endLabel, loopStartLabel, loopEndLabel, smoothLabel;
    juce::ToggleButton autoLoopBtn  { "Auto Loop" };
    juce::ToggleButton oneShotBtn   { "One-Shot Mode" };
    juce::ToggleButton playAcrossBtn{ "Play Across Keys" };
    juce::ComboBox     rootNoteBox;
    juce::ComboBox     categoryBox;
    juce::TextButton   previewBtn   { "Preview" };
    juce::TextButton   saveBtn      { "Save" };
    juce::TextButton   resetBtn     { "Reset To Original" };

    // ---- audio preview ----
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer  audioPlayer;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<juce::AudioTransportSource>    transport;
    juce::AudioFormatManager formatManager;
    bool deviceStarted = false;

    // ---- helpers ----
    static juce::File metaFileFor(const juce::File& audio);
    static CropMeta   readMeta   (const juce::File& audio);
    static void       writeMeta  (const CropMeta&  m);
    static juce::String inferCategory(const juce::File& f);
    static int          inferRootMidi(const juce::String& name, juce::String& outNoteText);

    void rebuildList();
    void selectIndex(int newIndex);
    void pushUiFromMeta(const CropMeta&);
    void pullMetaFromUi(CropMeta&);
    void persistSelected();
    void resetSelectedToOriginal();
    void startPreview();
    void stopPreview();
    void timerCallback() override;

    // ---- list model ----
    class ListModel : public juce::ListBoxModel
    {
    public:
        ListModel(AudioCropPanel& o) : owner(o) {}
        int  getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool sel) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    private:
        AudioCropPanel& owner;
    };
    std::unique_ptr<ListModel> listModel;
    std::vector<int> visibleIndices; // filtered view -> samples index

    void applyFilter();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioCropPanel)
};
