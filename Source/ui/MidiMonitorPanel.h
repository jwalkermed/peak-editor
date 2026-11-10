#pragma once

#include "peak/PeakConnection.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Phase 1 debugging view: port selection, live MIDI log, raw CC/SysEx sender,
// and saving captured incoming SysEx to .syx for reverse-engineering dumps.
class MidiMonitorPanel final : public juce::Component,
                               private juce::ChangeListener,
                               private juce::ListBoxModel
{
public:
    explicit MidiMonitorPanel (PeakConnection&);
    ~MidiMonitorPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static inline const juce::Colour panelColour { 0xff1e1f21 };
    static inline const juce::Colour accentColour { 0xff6fa8dc }; // Peak section-rule blue
    static inline const juce::Colour ledColour { 0xffffa53a };    // Peak amber

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void refreshPorts();
    void refreshLog();
    void sendCc();
    void sendSysexText();
    void saveCaptured();
    void setStatus (const juce::String& text, bool isError = false);

    PeakConnection& connection;
    std::vector<PeakConnection::LogEntry> rows;
    double startTimeMs = juce::Time::getMillisecondCounterHiRes();

    juce::Label inputLabel { {}, "MIDI In" }, outputLabel { {}, "MIDI Out" }, channelLabel { {}, "Channel" };
    juce::ComboBox inputBox, outputBox, channelBox;
    juce::TextButton rescanButton { "Rescan" };

    juce::Label ccLabel { {}, "CC" }, ccValueLabel { {}, "Value" };
    juce::Slider ccNumber { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Slider ccValue { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton sendCcButton { "Send CC" };

    juce::TextEditor sysexText;
    juce::TextButton sendSysexButton { "Send SysEx" };

    juce::ToggleButton showRealtime { "Show clock / sensing" }, pauseButton { "Pause" };
    juce::TextButton clearButton { "Clear log" }, saveButton { juce::String::fromUTF8 ("Save captured SysEx…") }, forgetButton { "Discard captured" };
    juce::Label capturedLabel, statusLabel;

    juce::ListBox logList { "MIDI log", this };
    juce::TextEditor detail;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiMonitorPanel)
};
