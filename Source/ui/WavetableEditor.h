#pragma once

#include "peak/PeakModel.h"
#include "peak/core/Wavetable.h"
#include "ui/CardStyle.h"
#include "peak/SamplePreview.h"
#include "ui/AudioSampleImporter.h"
#include "ui/WavetableBrowser.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

// The Wavetables tab: draws and edits a Peak user wavetable (5 waves, each up to 127 harmonics),
// reads User 1-10 from the Peak, imports WAV/.syx, saves .syx, and sends to a user slot. A sent
// wavetable plays at once but is only kept once it's saved on the Peak (Osc menu, page 9 "User
// Waves", Save), which sends no MIDI, so the slot stays marked "unsaved on Peak" until you say so.
class WavetableEditor final : public juce::Component,
                              private juce::ChangeListener,
                              private juce::Timer
{
public:
    explicit WavetableEditor (PeakModel&);
    ~WavetableEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override; // Cmd+Z / Shift+Cmd+Z
    void showHelp();                                   // the help card (the ? button)
    void browse (const juce::File& folder, const juce::File& select = {}); // the file browser at a folder, a file selected (a .syx loaded); for snapshots
    void browseHome (const juce::Array<juce::File>& folders);             // its Home list with these (not kept; for snapshots)
    void askHowToLoad (const juce::File&);              // an audio file: as a wavetable, or as a sample
    void openSample (const juce::File&, double startSeconds = 0.0); // the Import Audio Sample window

private:
    enum class Tool { pencil, line, shape };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override; // Live Edit: sends a moment after a change

    // Editing: every change is one undo step and, with Live Edit on, goes to the Peak.
    void beginEdit();
    void edited();
    void setWave (const peak::Wave&);
    void selectTool (Tool, peak::Shapes::Kind = peak::Shapes::sine);
    juce::Point<float> snapped (juce::Point<float>) const; // to the grid, when Snap to grid is on (line and shapes)
    void auditionPosition(); // Live Edit: the Peak's Wave Table Position follows the selected wave or the preview
    void undoRedo (bool undo);

    void loadFromPeak();
    void import();
    void importFile (const juce::File&); // a .syx, or a WAV/MP3 as a wavetable: from Import or the file browser
    void saveFile();
    void sendToPeak();
    void refreshSlots();
    void setStatus (const juce::String&, bool important = false);

    // Layout areas (set in resized()).
    juce::Rectangle<int> left, right, thumbs, editorArea, positionRow, harmonicsArea;
    juce::Rectangle<int> thumbBounds (int wave) const;
    int harmonicAt (juce::Point<int>) const;

    PeakModel& model;
    juce::SharedResourcePointer<SamplePreview> samplePreview; // plays through the editor's audio output (before the windows using it)
    peak::Wavetable table;
    int selectedWave = 0;
    std::vector<peak::Wavetable> undoStack, redoStack;
    std::array<bool, peak::Wavetable::slots> unsavedOnPeak {};

    // Drawing in progress.
    Tool tool = Tool::pencil;
    peak::Shapes::Kind stampKind = peak::Shapes::sine; // the shape drawn with Tool::shape
    std::vector<float> drawing, drawBase; // the samples being drawn (empty when not drawing), and before the line
    juce::Point<float> lastPoint, lineStart;
    bool draggingHarmonics = false, phaseView = false;
    int draggingWave = -1;          // a wave thumbnail being dragged onto another, to swap them
    juce::Point<int> dragPoint;     // where it's been dragged to
    int thumbAt (juce::Point<int>) const; // the wave thumbnail there, or -1
    float position = 0.0f; // Wave Table Position preview, 0-4

    juce::TextButton helpButton { "?" };
    std::unique_ptr<CardStyle::Overlay> helpOverlay;
    juce::TextButton undoButton { juce::String::fromUTF8 ("\xe2\x86\xb6") }, redoButton { juce::String::fromUTF8 ("\xe2\x86\xb7") };
    juce::TextEditor nameField;
    juce::ToggleButton liveEdit { "Live Edit" }, snapToGrid { "Snap to grid" };
    juce::ComboBox slotBox;
    juce::TextButton loadButton { juce::String::fromUTF8 ("Load from Peak\xe2\x80\xa6") }, importButton { juce::String::fromUTF8 ("Import\xe2\x80\xa6") },
        saveButton { juce::String::fromUTF8 ("Save\xe2\x80\xa6") }, sendButton { "Send to Peak" }, savedButton { "I've saved it on the Peak" };
    juce::TextButton pencilButton, lineButton, levelButton { "Level" }, phaseButton { "Phase" }, resetButton { "Reset wave" }, resetAllButton { "Reset all" };
    juce::OwnedArray<juce::TextButton> shapeButtons, opButtons;
    juce::Slider smoothing { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider positionSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label status;
    juce::MidiKeyboardComponent keyboard;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<WavetableBrowser> browser;
    std::unique_ptr<CardStyle::Overlay> importOverlay;
    void showOverlay (std::unique_ptr<juce::Component> card, std::function<void()> onClosed = {});

    // While the Import Audio Sample window is open: what it auditions can be sent to the Peak (all five
    // waves of the selected user slot set to that cycle). The slot is read from the Peak when the window
    // opens, and put back (with any oscillator the audition switched to it) unless something is imported.
    struct SampleSession
    {
        int slot = -1;
        bool readingOriginal = false, peakUsed = false;
        double openedMs = 0.0;
        std::optional<peak::Wavetable> original;           // the slot as read from the Peak
        std::vector<std::pair<const peak::ParamDef*, int>> oscillators; // before the audition changed them
        std::optional<peak::Wave> pending;                 // to send next
    };
    std::optional<SampleSession> session;
    juce::TimedCallback peakAudition { [this] { sendPeakAudition(); } };
    void sendPeakAudition();
    void restorePeak();                 // the slot and oscillators as they were
    void endSampleSession (bool imported);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableEditor)
};
