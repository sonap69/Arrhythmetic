#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "DrumEngine.h"
#include <set>

namespace Colours
{
    const juce::Colour bg       { 0xff17181b };
    const juce::Colour panel    { 0xff232428 };
    const juce::Colour well     { 0xff1c1d21 };   // recessed areas (effect blocks)
    const juce::Colour edge     { 0xff34363b };
    const juce::Colour ink      { 0xffe8e4da };
    const juce::Colour muted    { 0xff8d8b85 };
    const juce::Colour stepOff  { 0xff2e3035 };
    const juce::Colour control  { 0xff2f3136 };
    const juce::Colour led      { 0xffff4a2e };
    const juce::Colour g1       { 0xffd9452b };
    const juce::Colour g2       { 0xffe8892a };
    const juce::Colour g3       { 0xffe9c53a };
    const juce::Colour g4       { 0xffece6d6 };

    // one accent per effect: reverb, delay, compressor, distortion, stutter
    const juce::Colour fx[DrumEngine::numFx] = { juce::Colour (0xff7fb2d6), juce::Colour (0xff76c9b4),
                                                 g2, g1, g3 };
}

//==============================================================================
// Knobs drawn like the hardware: a value arc around a dark cap with a pointer.
// A slider with the "bipolar" property draws its arc from the centre (pitch).
class DrumLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DrumLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Font getSliderPopupFont (juce::Slider&) override;
};

//==============================================================================
// One effect in the FX panel: a title, an ON button and a few knobs.
class FxBlock : public juce::Component
{
public:
    FxBlock (const juce::String& title, juce::Colour accent, const juce::String& note = {});

    juce::Slider& addKnob (const juce::String& label, double min, double max, double defaultValue, double interval,
                           std::function<juce::String (double)> textFromValue);
    void setOn (bool on);

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::TextButton power { "ON" };
    std::vector<std::unique_ptr<juce::Slider>> knobs;

private:
    juce::String title, note;
    juce::Colour accent;
    juce::StringArray labels;
    bool isOn = false;
};

//==============================================================================
// The voice x step grid. Per row: voice name (click = preview, right-click = sound
// menu, drop an audio file = load it), volume and pitch knobs, mute, which effects
// the sound goes through, then the steps.
class StepGrid : public juce::Component,
                 public juce::FileDragAndDropTarget
{
public:
    explicit StepGrid (DrumEngine& e);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void setPlayhead (int step);
    void setGroups (const std::vector<int>& groupSizes);   // beat grouping in steps, e.g. {4,4,6} for 7/8
    void flashVoice (int v);
    void tick();             // called from the UI timer, ends key-press flashes
    void syncFromEngine();   // after loading a pattern: move the knobs to the engine's values

    std::function<void (int voice)> onSoundMenu;
    std::function<void (int voice, const juce::File&)> onFileDropped;

private:
    DrumEngine& engine;
    int playhead = -1;
    bool dragging = false, dragValue = false;
    int dragRow = -1, dropRow = -1;
    std::vector<int> groupOf;          // group index for each step
    std::array<juce::uint32, DrumEngine::numVoices> flashUntil {};
    std::array<juce::Slider, DrumEngine::numVoices> volumeKnobs, pitchKnobs;

    static constexpr int nameW = 140, knobW = 34, muteW = 28, fxW = 17, gap = 5, groupGap = 5, headerH = 20;

    int steps() const { return engine.numSteps.load(); }
    float rowHeight() const;
    float rowY (int row) const { return headerH + row * (rowHeight() + gap); }
    juce::Rectangle<float> nameRect (int row) const;
    juce::Rectangle<float> volumeRect (int row) const;
    juce::Rectangle<float> pitchRect (int row) const;
    juce::Rectangle<float> muteRect (int row) const;
    juce::Rectangle<float> fxRect (int row, int fx) const;
    juce::Rectangle<float> fxHeaderRect (int fx) const;
    float stepsX() const;
    juce::Rectangle<float> stepRect (int row, int step) const;
    bool hitStep (juce::Point<float>, int& row, int& step) const;
    int rowAt (float y) const;
    void setStep (int row, int step, bool on);
};

//==============================================================================
class MainComponent : public juce::AudioAppComponent,
                      private juce::Timer,
                      private juce::MidiInputCallback
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;
    void releaseResources() override {}

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void refreshMidiInputs();
    void togglePlay();
    void toggleRecord();
    void setTempo (double bpm);
    void loadPreset (int index);
    void onPatternChosen();
    void setTimeSignature (int index);
    void clearPattern();

    void showSoundMenu (int voice);
    void chooseSample (int voice);
    bool loadSample (int voice, const juce::File& file, bool remember, bool preview = true);
    void useBuiltInSound (int voice, bool preview = true);

    // effects
    void buildFxPanel();
    void setFxOn (int fx, bool on);
    void updateStutter();
    void syncFxPanelFromEngine();

    // saved patterns
    static juce::File patternFolder();
    void rebuildPatternMenu (int idToSelect);
    void showSaveDialog();
    void savePattern (const juce::String& name);
    void loadPatternFile (const juce::File& file);

    // export
    void showExportMenu();
    void chooseExportFile (bool midi, int bars);
    bool writeWav (const juce::File& file, int bars);
    bool writeMidi (const juce::File& file, int bars);

    DrumLookAndFeel lookAndFeel;   // first, so it outlives every component that uses it
    DrumEngine engine;
    StepGrid grid { engine };
    std::array<std::unique_ptr<FxBlock>, DrumEngine::numFx> fxBlocks;
    bool stutterLatched = false, stutterKeyHeld = false;
    int stutterKeyCode = 0;

    juce::TextButton playButton { "START" }, recButton { "REC" }, clearButton { "CLEAR" },
                     saveButton { "SAVE" }, exportButton { "EXPORT..." };
    juce::Slider swingSlider, volumeSlider;
    juce::Label tempoLabel, swingLabel, volumeLabel, presetLabel, meterLabel, bpmDisplay, hintLabel;
    juce::ComboBox presetBox, meterBox;

    juce::AudioFormatManager formats;
    std::vector<std::unique_ptr<Sample>> loadedSamples;   // kept alive for the audio thread
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::PropertiesFile> settings;

    juce::TooltipWindow tooltips { this };
    std::set<int> heldKeys;

    std::vector<juce::File> userPatterns;     // Pattern menu ids 1001+ map to these
    juce::String currentPatternName { "Boom bap" };
    std::unique_ptr<juce::AlertWindow> saveDialog;

    juce::String midiStatus;
    juce::Rectangle<int> midiStatusArea;
    std::array<std::atomic<bool>, DrumEngine::numVoices> midiFlash {};
    int midiScanCountdown = 0;
    uint32_t lastPatternVersion = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
