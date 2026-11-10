#pragma once

#include "PluginProcessor.h"
#include "ui/FrontPanel.h"
#include "ui/KeyboardStrip.h"
#include "ui/LibraryDrawer.h"
#include "ui/ModMatrixDrawer.h"
#include "ui/SettingsDrawer.h"
#include "ui/MidiMonitorPanel.h"

class PeakEditorEditor final : public juce::AudioProcessorEditor,
                               private juce::ChangeListener
{
public:
    explicit PeakEditorEditor (PeakEditorProcessor&);
    ~PeakEditorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override; // project patch offered/cleared

    PeakEditorProcessor& peakProcessor;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::ToggleButton forwardMidi { juce::String::fromUTF8 ("Track MIDI \xe2\x86\x92 Peak") };

    // Shown when the DAW project holds a patch that differs from the editor's.
    juce::Label projectMessage;
    juce::TextButton sendProjectPatch { "Send to Peak" }, loadProjectPatch { "Load in editor only" }, dismissProject { "Ignore" };
    juce::TooltipWindow tooltips { this, 400 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakEditorEditor)
};
