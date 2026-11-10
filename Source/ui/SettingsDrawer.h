#pragma once

#include "peak/PeakModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The Peak's global settings, grouped like its Settings pages. Values come from the last
// Settings dump (Backup page, Select = Settings, Go) and follow changes made on the Peak;
// edits are sent to the Peak as a whole Settings dump. MIDI channel and CC/NRPN are shown but locked,
// since changing them could cut the editor off. Local control is sent as CC 122.
class SettingsDrawer final : public juce::Component,
                             private juce::ChangeListener
{
public:
    explicit SettingsDrawer (PeakModel&);
    ~SettingsDrawer() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    PeakModel& model;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<std::unique_ptr<juce::Label>> pageTitles;

    juce::Label title { {}, "SETTINGS" }, status, hint, localLabel { {}, "Local" };
    juce::TextButton readButton { "How to read them" }, localOn { "On" }, localOff { "Off" };
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsDrawer)
};
